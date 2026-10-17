using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    public sealed class TransferResult
    {
        public string Source { get; internal set; }

        public string Destination { get; internal set; }

        public ulong? BytesTransferred { get; internal set; }

        public TimeSpan Elapsed { get; internal set; }
    }
}
