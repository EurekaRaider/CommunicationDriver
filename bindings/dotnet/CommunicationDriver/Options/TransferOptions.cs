using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    public sealed class TransferOptions
    {
        public TimeSpan? Timeout { get; set; }

        public bool Overwrite { get; set; } = true;
    }
}
