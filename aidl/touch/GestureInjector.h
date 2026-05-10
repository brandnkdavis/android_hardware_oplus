/*
 * SPDX-FileCopyrightText: 2025 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <atomic>
#include <thread>

namespace aidl {
namespace vendor {
namespace lineage {
namespace touch {

class GestureInjector {
  public:
    GestureInjector();
    ~GestureInjector();

  private:
    int mHbpCoreFd = -1;
    int mEventFd = -1;
    int mUinputFd = -1;
    std::atomic<bool> mRunning{false};
    std::thread mThread;

    std::string findTouchpanelDevice();
    bool createUinputDevice();
    void injectKeycode(int keycode);
    void run();
};

}  // namespace touch
}  // namespace lineage
}  // namespace vendor
}  // namespace aidl
