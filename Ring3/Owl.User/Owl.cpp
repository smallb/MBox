#include "stdafx.h"
#include "Owl.h"


namespace MBox
{
    static unsigned __stdcall MessageNotify(void* aParameter)
    {
        auto vThis = (Owl*)aParameter;
        return vThis->MessageNotify();
    }

    HRESULT Owl::Initialize()
    {
        HRESULT hr = S_OK;

        for (;;)
        {
            SetMessageNotifyCallback(std::bind(&Owl::MessageNotifyCallback,
                this,
                std::placeholders::_1,
                std::placeholders::_2,
                std::placeholders::_3,
                std::placeholders::_4),
                sizeof(MessageHeader),
                sizeof(ReplyHeader));

            break;
        }

        return hr;
    }

    void Owl::Uninitialize()
    {
        DestroyGetMessageThread();
        DisconnectCommunicationPort();
    }

    HRESULT Owl::CreateGetMessageThread()
    {
        HRESULT hr = S_OK;
        UINT32 vDosError = NOERROR;

        for (;;)
        {
            if (nullptr == m_MessagePacket)
            {
                m_MessagePacket = (MessageHeader*)new(std::nothrow) char[m_MessagePacketMaxBytes] {};
                if (nullptr == m_MessagePacket)
                {
                    vDosError = ERROR_OUTOFMEMORY;
                    break;
                }
            }

            if (nullptr == m_ReplyPacket)
            {
                m_ReplyPacket = (ReplyHeader*)new(std::nothrow) char[m_ReplyPacketMaxBytes] {};
                if (nullptr == m_ReplyPacket)
                {
                    vDosError = ERROR_OUTOFMEMORY;
                    break;
                }
            }

            m_EventHandles[EventClasses::NotifySemaphore] = CreateSemaphoreW(nullptr, 0, LONG_MAX, nullptr);
            if (nullptr == m_EventHandles[EventClasses::NotifySemaphore])
            {
                vDosError = GetLastError();
                break;
            }

            m_EventHandles[EventClasses::ThreadExit] = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (nullptr == m_EventHandles[EventClasses::ThreadExit])
            {
                vDosError = GetLastError();
                break;
            }

            m_NotifyThread = (HANDLE)_beginthreadex(nullptr, 0, MBox::MessageNotify, this, CREATE_SUSPENDED, nullptr);
            if (nullptr == m_NotifyThread)
            {
                vDosError = _doserrno;
                break;
            }

            break;
        }

        hr = HRESULT_FROM_WIN32(vDosError);
        return hr;
    }

    void Owl::DestroyGetMessageThread(UINT32 aSecondsForWait)
    {
        if (m_NotifyThread)
        {
            SetEvent(m_EventHandles[EventClasses::ThreadExit]);
            WaitForSingleObject(m_NotifyThread, aSecondsForWait * 1000);

            CloseHandle(m_NotifyThread);
            m_NotifyThread = nullptr;
        }

        for (UINT32 i = 0; i < EventClasses::Max; ++i)
        {
            if (m_EventHandles[i])
            {
                CloseHandle(m_EventHandles[i]);
                m_EventHandles[i] = nullptr;
            }
        }

        if (m_MessagePacket)
        {
            delete[](char*)(m_MessagePacket);
            m_MessagePacket = nullptr;
        }

        if (m_ReplyPacket)
        {
            delete[](char*)(m_ReplyPacket);
            m_ReplyPacket = nullptr;
        }
    }

    HRESULT Owl::ConnectCommunicationPort(
        PCWSTR aPortName,
        ConnectContext * aContext)
    {
        HRESULT hr = S_OK;
        UINT32  vDosError = NOERROR;

        static const wchar_t sDeviceDirectory[] = L"\\\\.\\Global";
        wchar_t* vDeviceName = nullptr;

        for (;;)
        {
            if (m_CommunicationPort)
            {
                break;
            }

            if (nullptr == aPortName
                || nullptr == aContext)
            {
                vDosError = ERROR_INVALID_PARAMETER;
                break;
            }

            auto vDeviceNameLength = wcslen(aPortName) + ARRAYSIZE(sDeviceDirectory) + ARRAYSIZE(L"\\");
            vDeviceName = new(std::nothrow) wchar_t[vDeviceNameLength] {};
            if (nullptr == vDeviceName)
            {
                vDosError = ERROR_OUTOFMEMORY;
                break;
            }

            hr = StringCchPrintfW(vDeviceName, vDeviceNameLength, L"%s%s", sDeviceDirectory, aPortName);
            if (FAILED(hr))
            {
                vDosError = hr;
                break;
            }

            m_CommunicationPort = CreateFile(
                vDeviceName,
                FILE_ANY_ACCESS,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                OPEN_EXISTING,
                0,
                nullptr);
            if (INVALID_HANDLE_VALUE == m_CommunicationPort)
            {
                vDosError = GetLastError();
                break;
            }

            aContext->m_Header.m_NotifySemaphore = UINT64(m_EventHandles[EventClasses::NotifySemaphore]);
            aContext->m_Header.m_ThreadHandle = UINT64(m_NotifyThread);

            UINT32 vReturnBytes = 0;
            if (!DeviceIoControl(
                m_CommunicationPort,
                UINT32(IoCode::Connecttion),
                aContext, aContext->m_ContextBytes + sizeof(ConnectContext),
                aContext, aContext->m_ContextBytes + sizeof(ConnectContext),
                (DWORD*)&vReturnBytes,
                nullptr))
            {
                vDosError = GetLastError();
                break;
            }

            ResumeThread(m_NotifyThread);

            m_IsConnected = true;
            vDosError = NOERROR;
            break;
        }

        hr = HRESULT_FROM_WIN32(vDosError);
        if (FAILED(hr))
        {
            DisconnectCommunicationPort();
        }

        return hr;
    }

    HRESULT Owl::DisconnectCommunicationPort()
    {
        HRESULT hr = S_OK;
        UINT32  vDosError = NOERROR;

        for (;;)
        {
            if (!m_CommunicationPort)
            {
                break;
            }

            if (m_IsConnected)
            {
                UINT32 vReturnBytes = 0;
                if (!DeviceIoControl(
                    m_CommunicationPort,
                    UINT32(IoCode::Disconnection),
                    nullptr, 0,
                    nullptr, 0,
                    (DWORD*)&vReturnBytes,
                    nullptr))
                {
                    vDosError = GetLastError();
                    break;
                }
                m_IsConnected = false;
            }

            CloseHandle(m_CommunicationPort);
            m_CommunicationPort = nullptr;

            break;
        }

        return hr;
    }

    HRESULT Owl::MessageNotify()
    {
        HRESULT hr = S_OK;

        for (;;)
        {
            auto vWaitResult = WaitForMultipleObjects(
                UINT32(EventClasses::Max),
                m_EventHandles,
                FALSE,
                INFINITE);

            if (EventClasses::ThreadExit == vWaitResult)
            {
                hr = S_OK;
                break;
            }
            else if (EventClasses::NotifySemaphore == vWaitResult)
            {
                if (false == m_IsConnected)
                {
                    continue;
                }

                hr = MessageHandler();
            }
            else if (WAIT_TIMEOUT == vWaitResult)
            {
                continue;
            }
            else
            {
                //
                // Other & WAIT_FAILED
                //

                hr = HRESULT_FROM_WIN32(GetLastError());
                break;
            }
        }

        _endthreadex(hr);
        return hr;
    }

    HRESULT Owl::SendMessage(
        void * aSenderBuffer,
        UINT32 aSenderBytes,
        void * aReplyBuffer,
        UINT32 aReplyBytes,
        UINT32 * aReturnedBytes)
    {
        HRESULT hr = S_OK;

        for (;;)
        {
            if (false == m_IsConnected)
            {
                hr = HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);
                break;
            }

            if (!DeviceIoControl(
                m_CommunicationPort,
                UINT32(IoCode::UserMessage),
                aSenderBuffer, aSenderBytes,
                aReplyBuffer, aReplyBytes,
                (DWORD*)aReturnedBytes,
                nullptr))
            {
                hr = HRESULT_FROM_WIN32(GetLastError());
                break;
            }

            break;
        }

        return hr;
    }

    HRESULT Owl::GetMessage(
        MessageHeader * aMessageBuffer,
        UINT32 aMessageBytes)
    {
        HRESULT hr = S_OK;

        for (;;)
        {
            if (false == m_IsConnected)
            {
                hr = HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);
                break;
            }

            UINT32 vReturnedBytes = 0;
            if (!DeviceIoControl(
                m_CommunicationPort,
                UINT32(IoCode::KernelRequest),
                nullptr, 0,
                aMessageBuffer, aMessageBytes,
                (DWORD*)&vReturnedBytes,
                nullptr))
            {
                hr = HRESULT_FROM_WIN32(GetLastError());
                break;
            }

            break;
        }

        return hr;
    }

    HRESULT Owl::ReplyMessage(
        ReplyHeader * aReplyBuffer,
        UINT32 aReplyBytes)
    {
        HRESULT hr = S_OK;

        for (;;)
        {
            if (false == m_IsConnected)
            {
                hr = HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);
                break;
            }

            UINT32 vReturnedBytes = 0;
            if (!DeviceIoControl(
                m_CommunicationPort,
                UINT32(IoCode::ReplyRequest),
                aReplyBuffer, aReplyBytes,
                nullptr, 0,
                (DWORD*)&vReturnedBytes,
                nullptr))
            {
                hr = HRESULT_FROM_WIN32(GetLastError());
                break;
            }

            break;
        }

        return hr;
    }

    HRESULT Owl::MessageHandler()
    {
        HRESULT hr = S_OK;

        for (;;)
        {
            if (nullptr == m_MessagePacket
                || nullptr == m_ReplyPacket)
            {
                hr = E_POINTER;
                break;
            }

            RtlSecureZeroMemory(m_MessagePacket, m_MessagePacketMaxBytes);
            RtlSecureZeroMemory(m_ReplyPacket, m_ReplyPacketMaxBytes);
            new(m_MessagePacket) MessageHeader;
            new(m_ReplyPacket) ReplyHeader;

            hr = GetMessage(m_MessagePacket, m_MessagePacketMaxBytes);
            if (FAILED(hr))
            {
                break;
            }

            hr = m_MessageNotifyCallback(
                m_MessagePacket,
                m_MessagePacketMaxBytes,
                m_ReplyPacket,
                m_ReplyPacketMaxBytes);

            m_ReplyPacket->m_MessageId = m_MessagePacket->m_MessageId;
            m_ReplyPacket->m_Status = STATUS_SUCCESS;
            hr = ReplyMessage(m_ReplyPacket, m_ReplyPacketMaxBytes);
            if (FAILED(hr))
            {
                break;
            }

            break;
        }

        return hr;
    }

    HRESULT Owl::MessageNotifyCallback(
        MessageHeader * /*aSenderBuffer*/,
        UINT32 /*aSenderBytes*/,
        ReplyHeader * /*aReplyBuffer*/,
        UINT32 /*aReplyBytes*/)
    {
        return E_NOTIMPL;
    }
}
