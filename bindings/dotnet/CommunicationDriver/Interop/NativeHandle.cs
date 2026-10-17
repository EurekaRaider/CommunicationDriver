using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Win32.SafeHandles;

namespace CommunicationDriver
{
    internal sealed class NativeHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        internal readonly int Kind;

        internal NativeHandle(IntPtr value, int kind)
            : base(true)
        {
            Kind = kind;
            SetHandle(value);
        }

        protected override bool ReleaseHandle()
        {
            switch (Kind)
            {
                case 0:
                    Native.cd_driver_release(handle);
                    break;
                case 1:
                    Native.cd_connection_release(handle);
                    break;
                case 2:
                    Native.cd_operation_release(handle);
                    break;
                case 3:
                    Native.cd_result_release(handle);
                    break;
                case 4:
                    Native.cd_cancel_source_release(handle);
                    break;
            }

            return true;
        }
    }
}
