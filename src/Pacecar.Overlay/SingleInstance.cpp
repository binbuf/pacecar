#include "SingleInstance.h"

#include "pacecar/app/AppIdentity.h"
#include "pacecar/util/Logger.h"

namespace pacecar::overlay
{
SingleInstance::~SingleInstance()
{
    if (hwnd_ != nullptr)
    {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (mutex_ != nullptr)
    {
        CloseHandle(mutex_);
        mutex_ = nullptr;
    }
}

bool SingleInstance::Acquire(std::wstring_view suffix, bool& alreadyRunning)
{
    alreadyRunning = false;
    suffix_.assign(suffix);
    instanceName_ = pacecar::app::MakeSingletonName(suffix_);
    mutex_ = CreateMutexW(nullptr, FALSE, instanceName_.c_str());
    if (mutex_ == nullptr)
    {
        LogWarn(L"single-instance: CreateMutex failed");
        return false;
    }
    alreadyRunning = GetLastError() == ERROR_ALREADY_EXISTS;
    return true;
}

bool SingleInstance::CreateActivationWindow(HINSTANCE instance, std::function<void()> onActivate)
{
    onActivate_ = std::move(onActivate);
    windowClass_ = pacecar::app::MakeActivationWindowClass(suffix_);
    messageName_ = pacecar::app::MakeActivationMessageName(suffix_);
    messageId_ = RegisterWindowMessageW(messageName_.c_str());
    if (messageId_ == 0)
    {
        LogWarn(L"single-instance: RegisterWindowMessage failed");
        return false;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = StaticWndProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = windowClass_.c_str();
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        LogWarn(L"single-instance: RegisterClassEx failed");
        return false;
    }

    hwnd_ = CreateWindowExW(0, windowClass_.c_str(), L"Pacecar activation", 0, 0, 0, 0, 0,
                            HWND_MESSAGE, nullptr, instance, this);
    if (hwnd_ == nullptr)
    {
        LogWarn(L"single-instance: activation window creation failed");
        return false;
    }
    return true;
}

bool SingleInstance::NotifyExisting(std::wstring_view suffix)
{
    const std::wstring windowClass = pacecar::app::MakeActivationWindowClass(suffix);
    HWND target = FindWindowExW(HWND_MESSAGE, nullptr, windowClass.c_str(), nullptr);
    if (target == nullptr)
    {
        return false;
    }
    const std::wstring messageName = pacecar::app::MakeActivationMessageName(suffix);
    const UINT messageId = RegisterWindowMessageW(messageName.c_str());
    if (messageId == 0)
    {
        return false;
    }
    return PostMessageW(target, messageId, 0, 0) != FALSE;
}

LRESULT CALLBACK SingleInstance::StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    SingleInstance* self = nullptr;
    if (message == WM_NCCREATE)
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<SingleInstance*>(create->lpCreateParams);
        if (self != nullptr)
        {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
        }
    }
    else
    {
        self = reinterpret_cast<SingleInstance*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self != nullptr)
    {
        return self->HandleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT SingleInstance::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (messageId_ != 0 && message == messageId_)
    {
        if (onActivate_)
        {
            onActivate_();
        }
        return 0;
    }
    if (message == WM_DESTROY)
    {
        hwnd_ = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}
} // namespace pacecar::overlay