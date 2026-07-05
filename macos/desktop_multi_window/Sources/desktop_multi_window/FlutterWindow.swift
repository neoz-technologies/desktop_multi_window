//
//  FlutterWindow.swift
//  flutter_multi_window
//
//  Created by Bin Yang on 2022/1/10.
//
import Cocoa
import FlutterMacOS
import Foundation

class BaseFlutterWindow: NSObject {
  private let window: NSWindow
  let windowChannel: WindowChannel

  // The main window this window is currently modal for, if any.
  private weak var modalParent: NSWindow?
  // Observer that keeps focus on this window while it is modal.
  private var modalKeyObserver: NSObjectProtocol?

  init(window: NSWindow, channel: WindowChannel) {
    self.window = window
    self.windowChannel = channel
    super.init()
  }

  // The underlying NSWindow, exposed so a window can be made modal for another.
  var nsWindow: NSWindow { window }

  func show() {
    window.makeKeyAndOrderFront(nil)
    NSApp.activate(ignoringOtherApps: true)
  }

  func hide() {
    window.orderOut(nil)
  }

  func center() {
    window.center()
  }

  func setFrame(frame: NSRect) {
    window.setFrame(frame, display: false, animate: true)
  }

  func setTitle(title: String) {
    window.title = title
  }

  func resizable(resizable: Bool) {
    if (resizable) {
      window.styleMask.insert(.resizable)
    } else {
      window.styleMask.remove(.resizable)
    }
  }

  func close() {
    window.close()
  }

  func setFrameAutosaveName(name: String) {
    window.setFrameAutosaveName(name)
  }

  // Make this window modal for `parent` (usually the main window).
  //
  // AppKit's real modal APIs (`runModal(for:)`, `beginModalSession`) all block
  // or require pumping the main thread, which is incompatible with the Flutter
  // engine's run loop. Instead we emulate a window-modal relationship:
  //   * the window becomes a child of the parent, so it always floats above it
  //     and follows it, and cannot be sent behind it;
  //   * whenever the parent tries to become key, focus is bounced straight back
  //     to this window, so the parent can no longer be focused or interacted
  //     with until the modal window is dismissed.
  func setModal(modal: Bool, parent: NSWindow?) {
    if modal {
      guard modalParent == nil, let parent = parent else { return }
      modalParent = parent
      parent.addChildWindow(window, ordered: .above)
      window.makeKeyAndOrderFront(nil)
      modalKeyObserver = NotificationCenter.default.addObserver(
        forName: NSWindow.didBecomeKeyNotification,
        object: parent,
        queue: .main
      ) { [weak self] _ in
        guard let self = self, self.modalParent != nil else { return }
        NSSound.beep()
        self.window.makeKeyAndOrderFront(nil)
      }
    } else {
      clearModal()
    }
  }

  // Tear down the modal relationship. Safe to call multiple times. Must be
  // called before the window closes so the parent window is released again.
  func clearModal() {
    if let observer = modalKeyObserver {
      NotificationCenter.default.removeObserver(observer)
      modalKeyObserver = nil
    }
    if let parent = modalParent {
      parent.removeChildWindow(window)
      modalParent = nil
    }
  }
}

class FlutterWindow: BaseFlutterWindow {
  let windowId: Int64

  let window: NSWindow

  weak var delegate: WindowManagerDelegate?

  init(id: Int64, arguments: String) {
    windowId = id
    window = NSWindow(
      contentRect: NSRect(x: 0, y: 0, width: 480, height: 270),
      styleMask: [.miniaturizable, .closable, .resizable, .titled, .fullSizeContentView],
      backing: .buffered, defer: false)
    let project = FlutterDartProject()
    project.dartEntrypointArguments = ["multi_window", "\(windowId)", arguments]
    let flutterViewController = FlutterViewController(project: project)
    window.contentViewController = flutterViewController

    let plugin = flutterViewController.registrar(forPlugin: "FlutterMultiWindowPlugin")
    FlutterMultiWindowPlugin.registerInternal(with: plugin)
    let windowChannel = WindowChannel.register(with: plugin, windowId: id)
    // Give app a chance to register plugin.
    FlutterMultiWindowPlugin.onWindowCreatedCallback?(flutterViewController)

    super.init(window: window, channel: windowChannel)

    window.delegate = self
    window.isReleasedWhenClosed = false
    window.titleVisibility = .hidden
    window.titlebarAppearsTransparent = true
  }

  deinit {
    debugPrint("release window resource")
    window.delegate = nil
    if let flutterViewController = window.contentViewController as? FlutterViewController {
      flutterViewController.engine.shutDownEngine()
    }
    window.contentViewController = nil
    window.windowController = nil
  }
}

extension FlutterWindow: NSWindowDelegate {
  func windowWillClose(_ notification: Notification) {
    // Tear down the modal relationship (if any) before the window goes away so
    // the parent window becomes interactive again.
    clearModal()
    delegate?.onClose(windowId: windowId)
  }

  func windowShouldClose(_ sender: NSWindow) -> Bool {
    delegate?.onClose(windowId: windowId)
    return true
  }
}
