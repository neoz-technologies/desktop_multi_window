//
// Created by yangbin on 2022/1/11.
//

#ifndef DESKTOP_MULTI_WINDOW_WINDOWS_FLUTTER_WINDOW_H_
#define DESKTOP_MULTI_WINDOW_WINDOWS_FLUTTER_WINDOW_H_

#include <Windows.h>

#include <flutter/flutter_view_controller.h>

#include <cstdint>
#include <memory>

#include "base_flutter_window.h"
#include "window_channel.h"

class FlutterWindowCallback {

 public:
  virtual void OnWindowClose(int64_t id) = 0;

  virtual void OnWindowDestroy(int64_t id) = 0;

};

class FlutterWindow : public BaseFlutterWindow {

 public:

  FlutterWindow(HWND main_window_handle, int64_t id, std::string args, const std::shared_ptr<FlutterWindowCallback> &callback);
  ~FlutterWindow() override;

  WindowChannel *GetWindowChannel() override {
    return window_channel_.get();
  }

  void SetModal(bool modal) override;

 protected:

  HWND GetWindowHandle() override { return window_handle_; }

 private:

  std::weak_ptr<FlutterWindowCallback> callback_;

  HWND window_handle_;

  // The owner (main) window that is locked while this window is modal.
  HWND owner_handle_;

  // Whether this window currently locks its owner as a modal window.
  bool is_modal_ = false;

  int64_t id_;

  // The Flutter instance hosted by this window.
  std::unique_ptr<flutter::FlutterViewController> flutter_controller_;

  std::unique_ptr<WindowChannel> window_channel_;

  double scale_factor_;

  bool destroyed_ = false;

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

  static FlutterWindow *GetThisFromHandle(HWND window) noexcept;

  // Subclass installed on the owner window while this window is modal. Clicks
  // on the locked owner are swallowed there and bring this window forward.
  static LRESULT CALLBACK OwnerSubclassProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                                            UINT_PTR subclass_id, DWORD_PTR reference_data);

  LRESULT MessageHandler(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

  // Re-enables the owner window and returns activation to it if this window is
  // currently modal. Safe to call when the window is not modal.
  void ReleaseModal();

  void Destroy();
};

#endif //DESKTOP_MULTI_WINDOW_WINDOWS_FLUTTER_WINDOW_H_
