#include "stdafx.h"
#include "WFPFlt.h"
#include "WFPFlt.Manager.EngineState.h"
#include "WFPFlt.Manager.Engine.h"
#include "WFPFlt.Manager.Injection.h"
#include "WFPFlt.Manager.Filter.h"
#include "WFPFlt.Manager.Provider.h"
#include "WFPFlt.Manager.Redirect.h"

#include <KBasic\KBasic.System.h>

namespace MBox
{
    namespace WFPFlt
    {
        volatile long           s_IsStartedFilter   = FALSE;
        static DEVICE_OBJECT*   s_DeviceObject      = nullptr;

        BOOLEAN IsSupportedWFP()
        {
            if (KBasic::System::GetSystemVersion() >= SystemVersion::WindowsVista)
            {
                return TRUE;
            }
            return FALSE;
        }

        static void StateChangeCallback (EngineStateManager::StateChangeCallbackParameter* aParameter)
        {
            switch (aParameter->m_State)
            {
            default:
                break;

            case FWPM_SERVICE_STATE::FWPM_SERVICE_START_PENDING:
                break;

            case FWPM_SERVICE_STATE::FWPM_SERVICE_RUNNING:
                break;

            case FWPM_SERVICE_STATE::FWPM_SERVICE_STOP_PENDING:
                break;

            case FWPM_SERVICE_STATE::FWPM_SERVICE_STOPPED:
            {
                GetEngineManager()->CloseEngine();
                break;
            }

            }
        }

        static NTSTATUS InitializeManager()
        {
            NTSTATUS vStatus = STATUS_INSUFFICIENT_RESOURCES;

            for (;;)
            {
                vStatus = GetEngineStateManager()->Initialize();
                if (!NT_SUCCESS(vStatus))
                {
                    break;
                }

                vStatus = GetEngineManager()->Initialize();
                if (!NT_SUCCESS(vStatus))
                {
                    break;
                }

                vStatus = GetInjectionManager()->Initialize();
                if (!NT_SUCCESS(vStatus))
                {
                    break;
                }

                if (KBasic::System::GetSystemVersion() >= SystemVersion::Windows8)
                {
                    vStatus = GetProviderManager()->Initialize();
                    if (!NT_SUCCESS(vStatus))
                    {
                        break;
                    }

                    vStatus = GetRedirectManager()->Initialize();
                    if (!NT_SUCCESS(vStatus))
                    {
                        break;
                    }
                }


                vStatus = STATUS_SUCCESS;
                break;
            }

            if (!NT_SUCCESS(vStatus))
            {
                GetRedirectManager()->Uninitialize();
                GetProviderManager()->Uninitialize();
                GetInjectionManager()->Uninitialize();
                GetEngineManager()->Uninitialize();
                GetEngineStateManager()->Uninitialize();
            }

            return vStatus;
        }

        NTSTATUS Initialize(
            DRIVER_OBJECT* aDriverObject,
            UNICODE_STRING* /*aRegistryPath*/,
            DEVICE_OBJECT* aDeviceObject)
        {
            NTSTATUS vStatus = STATUS_SUCCESS;

            for (;;)
            {
                if (FALSE == IsSupportedWFP())
                {
                    vStatus = STATUS_NOT_SUPPORTED;
                }

                if (nullptr == aDeviceObject)
                {
                    vStatus = IoCreateDevice(
                        aDriverObject,
                        0,
                        nullptr,
                        FILE_DEVICE_NETWORK,
                        FILE_DEVICE_SECURE_OPEN | FILE_AUTOGENERATED_DEVICE_NAME,
                        FALSE,
                        &s_DeviceObject);
                    if (!NT_SUCCESS(vStatus))
                    {
                        break;
                    }
                }

                vStatus = InitializeManager();
                if (!NT_SUCCESS(vStatus))
                {
                    break;
                }

                auto vEngineStateManager = GetEngineStateManager();
                vStatus = GetEngineStateManager()->RegisterStateChangeNotify(s_DeviceObject, StateChangeCallback, nullptr);
                if (!NT_SUCCESS(vStatus))
                {
                    break;
                }

                if (FWPM_SERVICE_STATE::FWPM_SERVICE_RUNNING != vEngineStateManager->GetEngineState())
                {
                    vStatus = STATUS_PENDING;
                    break;
                }

                auto vEngineManager = GetEngineManager();
                vStatus = vEngineManager->OpenEngine();
                if (!NT_SUCCESS(vStatus))
                {
                    break;
                }

                auto vInjectionManager = GetInjectionManager();
                vStatus = vInjectionManager->CreateInjectionHandle();
                if (!NT_SUCCESS(vStatus))
                {
                    break;
                }

                break;
            }

            if (!NT_SUCCESS(vStatus))
            {
                Unitialize();
            }

            return vStatus;
        }

        void Unitialize()
        {
            //
            // Unitialize the order
            //
            // Callout 
            // Redirect
            // Injection
            // Engine
            // EngineState
            //

            GetInjectionManager()->Uninitialize();
            GetEngineManager()->Uninitialize();
            GetEngineStateManager()->Uninitialize();

            if (s_DeviceObject)
            {
                IoDeleteDevice(s_DeviceObject);
                s_DeviceObject = nullptr;
            }
        }


        NTSTATUS StartFilter()
        {
            InterlockedExchange(&s_IsStartedFilter, TRUE);
            return STATUS_SUCCESS;
        }

        NTSTATUS StopFilter()
        {
            InterlockedExchange(&s_IsStartedFilter, FALSE);
            return STATUS_SUCCESS;
        }

        BOOLEAN IsStartedFilter()
        {
            return BOOLEAN(s_IsStartedFilter);
        }

    }
}
