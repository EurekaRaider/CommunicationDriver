using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    public enum BatchItemState
    {
        Completed,
        Failed,
        TimedOut,
        Cancelled,
        NotStarted,
    }
}
