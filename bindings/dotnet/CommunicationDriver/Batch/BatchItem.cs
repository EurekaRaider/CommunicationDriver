using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    public sealed class BatchItem
    {
        internal int Kind;
        internal string Command,
            Source,
            Destination;
        internal byte[] Bytes;
        internal Check Check;
        internal CallOptions Options;
        internal TransferOptions TransferOptions;

        public string Key { get; private set; }

        public Connection Connection { get; private set; }

        private BatchItem(string key, Connection connection, int kind)
        {
            Key = key ?? throw new ArgumentNullException(nameof(key));
            Connection = connection ?? throw new ArgumentNullException(nameof(connection));
            Kind = kind;
        }

        public static BatchItem Execute(
            string key,
            Connection connection,
            string command,
            Check check = null,
            CallOptions options = null
        )
        {
            return new BatchItem(key, connection, 0)
            {
                Command = command ?? throw new ArgumentNullException(nameof(command)),
                Check = check,
                Options = options,
            };
        }

        public static BatchItem Execute(
            string key,
            Connection connection,
            byte[] command,
            Check check = null,
            CallOptions options = null
        )
        {
            return new BatchItem(key, connection, 1)
            {
                Bytes = (byte[])
                    (command ?? throw new ArgumentNullException(nameof(command))).Clone(),
                Check = check,
                Options = options,
            };
        }

        public static BatchItem ExecuteUntil(
            string key,
            Connection connection,
            string command,
            Check match,
            CallOptions options = null
        )
        {
            var item = Execute(
                key,
                connection,
                command,
                match ?? throw new ArgumentNullException(nameof(match)),
                options
            );
            item.Kind = 2;

            return item;
        }

        public static BatchItem ExecuteUntil(
            string key,
            Connection connection,
            byte[] command,
            Check match,
            CallOptions options = null
        )
        {
            var item = Execute(
                key,
                connection,
                command,
                match ?? throw new ArgumentNullException(nameof(match)),
                options
            );
            item.Kind = 3;

            return item;
        }

        public static BatchItem Upload(
            string key,
            Connection connection,
            string localPath,
            string remotePath,
            TransferOptions options = null
        )
        {
            return new BatchItem(key, connection, 4)
            {
                Source = localPath,
                Destination = remotePath,
                TransferOptions = options,
            };
        }

        public static BatchItem Download(
            string key,
            Connection connection,
            string remotePath,
            string localPath,
            TransferOptions options = null
        )
        {
            return new BatchItem(key, connection, 5)
            {
                Source = remotePath,
                Destination = localPath,
                TransferOptions = options,
            };
        }
    }
}
