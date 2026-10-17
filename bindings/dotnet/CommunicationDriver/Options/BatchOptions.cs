using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    public sealed class BatchOptions
    {
        public uint MaxConcurrency { get; set; } = 32;
    }
}
