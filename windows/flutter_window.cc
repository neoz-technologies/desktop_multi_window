//
// Created by yangbin on 2022/1/11.
//

#include "flutter_window.h"

#include "flutter_windows.h"

#include "tchar.h"

#include <commctrl.h>

#include <iostream>
#include <map>
#include <utility>

#include "include/desktop_multi_window/desktop_multi_window_plugin.h"
#include "multi_window_plugin_internal.h"

namespace {

WindowCreatedCallback _g_window_created_callback = nullptr;

TCHAR kFlutterWindowClassName[] = _T("FlutterMultiWindow");

// Class of the child window Flutter renders a view into.
TCHAR kFlutterViewClassName[] = _T("FLUTTERVIEW");

// How many modal windows a given owner window currently has. The owner is only
// unlocked again once the last of them is gone.
std::map<HWND, int> &ModalCounts() {
  static std::map<HWND, int> counts;
  return counts;
}

// Returns the child window that hosts `window`'s Flutter content, or the
// window's first child if Flutter ever renames its view class.
HWND GetContentView(HWND window) {
  if (!window) {
    return nullptr;
  }
  HWND view = FindWindowEx(window, nullptr, kFlutterViewClassName, nullptr);
  return view ? view : GetWindow(window, GW_CHILD);
}

int32_t class_registered_ = 0;

void RegisterWindowClass(WNDPROC wnd_proc) {
  if (class_registered_ == 0) {
    WNDCLASS window_class{};
    window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    window_class.lpszClassName = kFlutterWindowClassName;
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.cbClsExtra = 0;
    window_class.cbWndExtra = 0;
    window_class.hInstance = GetModuleHandle(nullptr);
    window_class.hIcon =
        LoadIcon(window_class.hInstance, IDI_APPLICATION);
    window_class.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);
    window_class.lpszMenuName = nullptr;
    window_class.lpfnWndProc = wnd_proc;
    RegisterClass(&window_class);
  }
  class_registered_++;
}

void UnregisterWindowClass() {
  class_registered_--;
  if (class_registered_ != 0) {
    return;
  }
  UnregisterClass(kFlutterWindowClassName, nullptr);
}

// Scale helper to convert logical scaler values to physical using passed in
// scale factor
inline int Scale(int source, double scale_factor) {
  return static_cast<int>(source * scale_factor);
}

using EnableNonClientDpiScaling = BOOL __stdcall(HWND hwnd);

// Dynamically loads the |EnableNonClientDpiScaling| from the User32 module.
// This API is only needed for PerMonitor V1 awareness mode.
void EnableFullDpiSupportIfAvailable(HWND hwnd) {
  HMODULE user32_module = LoadLibraryA("User32.dll");
  if (!user32_module) {
    return;
  }
  auto enable_non_client_dpi_scaling =
      reinterpret_cast<EnableNonClientDpiScaling *>(
          GetProcAddress(user32_module, "EnableNonClientDpiScaling"));
  if (enable_non_client_dpi_scaling != nullptr) {
    enable_non_client_dpi_scaling(hwnd);
    FreeLibrary(user32_module);
  }
}

}

FlutterWindow::FlutterWindow(
    HWND main_window_handle,
    int64_t id,
    std::string args,
    const std::shared_ptr<FlutterWindowCallback> &callback
) : callback_(callback), id_(id), window_handle_(nullptr), owner_handle_(main_window_handle), scale_factor_(1) {
  RegisterWindowClass(FlutterWindow::WndProc);

  const POINT target_point = {static_cast<LONG>(10),
                              static_cast<LONG>(10)};
  HMONITOR monitor = MonitorFromPoint(target_point, MONITOR_DEFAULTTONEAREST);
  UINT dpi = FlutterDesktopGetDpiForMonitor(monitor);
  scale_factor_ = dpi / 96.0;

  HWND window_handle = CreateWindow(
      kFlutterWindowClassName, L"", WS_POPUPWINDOW,
      Scale(target_point.x, scale_factor_), Scale(target_point.y, scale_factor_),
      Scale(1280, scale_factor_), Scale(720, scale_factor_),
      main_window_handle, nullptr, GetModuleHandle(nullptr), this);

  RECT frame;
  GetClientRect(window_handle, &frame);
  flutter::DartProject project(L"data");
  project.set_dart_entrypoint_arguments({"multi_window", std::to_string(id), std::move(args)});
  flutter_controller_ = std::make_unique<flutter::FlutterViewController>(
      frame.right - frame.left, frame.bottom - frame.top, project);
  // Ensure that basic setup of the controller was successful.
  if (!flutter_controller_->engine() || !flutter_controller_->view()) {
    std::cerr << "Failed to setup FlutterViewController." << std::endl;
  }
  auto view_handle = flutter_controller_->view()->GetNativeWindow();
  SetParent(view_handle, window_handle);
  MoveWindow(view_handle, 0, 0, frame.right - frame.left, frame.bottom - frame.top, true);

  InternalMultiWindowPluginRegisterWithRegistrar(
      flutter_controller_->engine()->GetRegistrarForPlugin("DesktopMultiWindowPlugin"));
  window_channel_ = WindowChannel::RegisterWithRegistrar(
      flutter_controller_->engine()->GetRegistrarForPlugin("DesktopMultiWindowPlugin"), id_);

  if (_g_window_created_callback) {
    _g_window_created_callback(flutter_controller_.get());
  }

  // hide the window when created.
  ShowWindow(window_handle, SW_HIDE);

}

// static
FlutterWindow *FlutterWindow::GetThisFromHandle(HWND window) noexcept {
  return reinterpret_cast<FlutterWindow *>(
      GetWindowLongPtr(window, GWLP_USERDATA));
}

// static
LRESULT CALLBACK FlutterWindow::WndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    auto window_struct = reinterpret_cast<CREATESTRUCT *>(lparam);
    SetWindowLongPtr(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window_struct->lpCreateParams));

    auto that = static_cast<FlutterWindow *>(window_struct->lpCreateParams);
    EnableFullDpiSupportIfAvailable(window);
    that->window_handle_ = window;
  } else if (FlutterWindow *that = GetThisFromHandle(window)) {
    return that->MessageHandler(window, message, wparam, lparam);
  }

  return DefWindowProc(window, message, wparam, lparam);
}

LRESULT FlutterWindow::MessageHandler(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {

  // Give Flutter, including plugins, an opportunity to handle window messages.
  if (flutter_controller_) {
    std::optional<LRESULT> result = flutter_controller_->HandleTopLevelWindowProc(hwnd, message, wparam, lparam);
    if (result) {
      return *result;
    }
  }

  auto child_content_ = flutter_controller_ ? flutter_controller_->view()->GetNativeWindow() : nullptr;

  switch (message) {
    case WM_FONTCHANGE: {
      flutter_controller_->engine()->ReloadSystemFonts();
      break;
    }
    case WM_DESTROY: {
      Destroy();
      if (!destroyed_) {
        destroyed_ = true;
        if (auto callback = callback_.lock()) {
          callback->OnWindowDestroy(id_);
        }
      }
      return 0;
    }
    case WM_CLOSE: {
      // Re-enable the owner before this window is destroyed so focus returns
      // to it and it does not stay permanently disabled.
      ReleaseModal();
      if (auto callback = callback_.lock()) {
        callback->OnWindowClose(id_);
      }
      break;
    }
    case WM_DPICHANGED: {
      auto newRectSize = reinterpret_cast<RECT *>(lparam);
      LONG newWidth = newRectSize->right - newRectSize->left;
      LONG newHeight = newRectSize->bottom - newRectSize->top;

      SetWindowPos(hwnd, nullptr, newRectSize->left, newRectSize->top, newWidth,
                   newHeight, SWP_NOZORDER | SWP_NOACTIVATE);

      return 0;
    }
    case WM_SIZE: {
      RECT rect;
      GetClientRect(window_handle_, &rect);
      if (child_content_ != nullptr) {
        // Size and position the child window.
        MoveWindow(child_content_, rect.left, rect.top, rect.right - rect.left,
                   rect.bottom - rect.top, TRUE);
      }
      return 0;
    }

    case WM_ACTIVATE: {
      if (child_content_ != nullptr) {
        SetFocus(child_content_);
      }
      return 0;
    }
    default: break;
  }

  return DefWindowProc(window_handle_, message, wparam, lparam);
}

void FlutterWindow::SetModal(bool modal) {
  // The owner is normally captured at construction time. Fall back to the
  // window's registered owner just in case it was not known back then, so a
  // missing owner cannot silently turn the modal request into a no-op.
  if (!owner_handle_) {
    owner_handle_ = GetWindow(window_handle_, GW_OWNER);
  }
  if (!owner_handle_ || modal == is_modal_) {
    return;
  }
  if (!modal) {
    ReleaseModal();
    return;
  }
  is_modal_ = true;
  ModalCounts()[owner_handle_]++;
  // Disable the owner's *content view*, not the owner window itself. Disabling
  // the top level window is the textbook Win32 way to be modal, but Windows
  // answers every click on a disabled top level window with the system
  // "not allowed" beep - which is what the user hears for every click next to
  // the modal window. A click on a disabled child window is silently handed to
  // its parent instead, so the owner is just as inert, without the noise.
  HWND owner_view = GetContentView(owner_handle_);
  if (owner_view) {
    EnableWindow(owner_view, FALSE);
    if (GetFocus() == owner_view) {
      SetFocus(nullptr);
    }
  }
  // With the owner window itself still enabled, clicks on it have to be
  // answered by hand: swallow them and bring this window forward, the way a
  // real modal dialog does.
  SetWindowSubclass(owner_handle_, FlutterWindow::OwnerSubclassProc,
                    reinterpret_cast<UINT_PTR>(this),
                    reinterpret_cast<DWORD_PTR>(this));
  // Disabling input is not enough on its own: if the main window keeps the
  // activation/foreground it still looks and feels focusable. Explicitly pull
  // this window in front of the owner and take the activation so the main
  // window can no longer be brought forward or focused.
  if (window_handle_) {
    ShowWindow(window_handle_, SW_SHOW);
    BringWindowToTop(window_handle_);
    SetForegroundWindow(window_handle_);
    SetActiveWindow(window_handle_);
  }
}

// static
LRESULT CALLBACK FlutterWindow::OwnerSubclassProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                                                  UINT_PTR subclass_id, DWORD_PTR reference_data) {
  auto *window = reinterpret_cast<FlutterWindow *>(reference_data);
  switch (message) {
    case WM_MOUSEACTIVATE:
      if (window && window->is_modal_ && window->window_handle_) {
        SetForegroundWindow(window->window_handle_);
        return MA_NOACTIVATEANDEAT;
      }
      break;
    case WM_NCDESTROY:
      // The owner is going away before the modal window did; never leave a
      // subclass pointing at this object behind.
      RemoveWindowSubclass(hwnd, FlutterWindow::OwnerSubclassProc, subclass_id);
      break;
    default:break;
  }
  return DefSubclassProc(hwnd, message, wparam, lparam);
}

void FlutterWindow::ReleaseModal() {
  if (!is_modal_) {
    return;
  }
  is_modal_ = false;
  if (!owner_handle_) {
    return;
  }
  RemoveWindowSubclass(owner_handle_, FlutterWindow::OwnerSubclassProc,
                       reinterpret_cast<UINT_PTR>(this));
  auto &counts = ModalCounts();
  auto count = counts.find(owner_handle_);
  if (count != counts.end() && --count->second > 0) {
    // Another modal window is still open on the same owner; it stays locked.
    return;
  }
  if (count != counts.end()) {
    counts.erase(count);
  }
  // Re-enable the owner's view *before* handing activation back, otherwise the
  // still-disabled view cannot take the keyboard focus again.
  HWND owner_view = GetContentView(owner_handle_);
  if (owner_view) {
    EnableWindow(owner_view, TRUE);
  }
  SetForegroundWindow(owner_handle_);
  SetActiveWindow(owner_handle_);
  if (owner_view) {
    SetFocus(owner_view);
  }
}

void FlutterWindow::Destroy() {
  // Safety net: never leave the owner disabled if the window is destroyed
  // without going through WM_CLOSE.
  ReleaseModal();
  if (window_channel_) {
    window_channel_ = nullptr;
  }
  if (flutter_controller_) {
    flutter_controller_ = nullptr;
  }
  if (window_handle_) {
    DestroyWindow(window_handle_);
    window_handle_ = nullptr;
  }
}

FlutterWindow::~FlutterWindow() {
  if (window_handle_) {
    std::cout << "window_handle leak." << std::endl;
  }
  UnregisterWindowClass();
}

void DesktopMultiWindowSetWindowCreatedCallback(WindowCreatedCallback callback) {
  _g_window_created_callback = callback;
}