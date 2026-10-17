using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Win32.SafeHandles;

namespace CommunicationDriver
{
    internal sealed class HandleLease : IDisposable
    {
        internal NativeHandle Handle;
        private bool added;

        internal HandleLease(NativeHandle handle)
        {
            Handle = handle;
            handle.DangerousAddRef(ref added);
        }

        internal IntPtr Pointer => Handle.DangerousGetHandle();

        public void Dispose()
        {
            if (added)
            {
                added = false;
                Handle.DangerousRelease();
            }
        }
    }
}
