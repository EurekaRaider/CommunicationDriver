using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    [Flags]
    public enum Capabilities : ulong
    {
        Read = 1,
        Write = 2,
        Execute = 4,
        ExecuteUntil = 8,
        CloseInput = 16,
        StandardError = 32,
        Upload = 64,
        Download = 128,
        Message = 256,
    }
}
