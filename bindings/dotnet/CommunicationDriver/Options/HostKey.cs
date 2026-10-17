using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    public sealed class HostKey
    {
        internal int Kind;
        internal string Value;

        private HostKey() { }

        public static HostKey KnownHosts(string path)
        {
            return new HostKey { Value = path ?? throw new ArgumentNullException(nameof(path)) };
        }

        public static HostKey Sha256(string fingerprint)
        {
            return new HostKey
            {
                Kind = 1,
                Value = fingerprint ?? throw new ArgumentNullException(nameof(fingerprint)),
            };
        }
    }
}
