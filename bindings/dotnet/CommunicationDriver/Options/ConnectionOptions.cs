using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    public abstract class ConnectionOptions
    {
        public EncodingKind Encoding { get; set; } = EncodingKind.UTF8;

        public string NewLine { get; set; } = "\n";

        public ReceiveUntil ReceiveUntil { get; set; }

        public TimeSpan ConnectTimeout { get; set; } = TimeSpan.FromSeconds(10);

        public TimeSpan IoTimeout { get; set; } = TimeSpan.FromSeconds(5);

        public TimeSpan ExecutionTimeout { get; set; } = TimeSpan.FromSeconds(30);

        public TimeSpan CloseTimeout { get; set; } = TimeSpan.FromSeconds(10);

        public ulong MaxResponseBytes { get; set; } = 1048576;
    }
}
