using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    public enum ErrorCode
    {
        OK,
        InvalidArgument,
        NotSupported,
        NotOpen,
        Closed,
        Timeout,
        Cancelled,
        ConnectionFailed,
        Disconnected,
        EndOfStream,
        AuthenticationFailed,
        HostKeyRejected,
        DeviceUnavailable,
        PermissionDenied,
        IOError,
        ProtocolError,
        EncodingError,
        LimitExceeded,
        Busy,
        NeedsRecovery,
        InvalidContext,
        InternalError,
    }
}
