using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    public sealed class SshAuth
    {
        internal int Kind;
        internal string Value,
            Passphrase;

        private SshAuth() { }

        public static SshAuth Password(string password)
        {
            return new SshAuth
            {
                Kind = 0,
                Value = password ?? throw new ArgumentNullException(nameof(password)),
            };
        }

        public static SshAuth PrivateKey(string path, string passphrase = null)
        {
            return new SshAuth
            {
                Kind = 1,
                Value = path ?? throw new ArgumentNullException(nameof(path)),
                Passphrase = passphrase,
            };
        }
    }
}
