using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    public enum ConnectionState
    {
        Created,
        Opening,
        Open,
        Closing,
        Closed,
        Faulted,
        NeedsRecovery,
    }
}
