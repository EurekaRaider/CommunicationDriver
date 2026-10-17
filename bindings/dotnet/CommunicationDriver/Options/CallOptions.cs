using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    public sealed class CallOptions
    {
        public TimeSpan? Timeout { get; set; }

        public EncodingKind? Encoding { get; set; }

        public string NewLine { get; set; }

        public ulong MaxResponseBytes { get; set; }

        public ReceiveUntil ReceiveUntil { get; set; }

        public StreamKind ReadStream { get; set; }

        public StreamKind CheckStream { get; set; }
    }
}
