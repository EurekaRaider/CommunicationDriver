using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    public enum CompletionReason
    {
        None,
        CommandCompleted,
        ProtocolBoundary,
        Idle,
        Matched,
    }
}
