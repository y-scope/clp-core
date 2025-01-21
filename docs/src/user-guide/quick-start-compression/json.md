# Compressing JSON logs

To compress JSON logs, from inside the package directory, run:

```bash
sbin/compress.sh fs --timestamp-key '<timestamp-key>' <path1> [<path2> ...]
```

* `fs` is a subcommand for compressing logs from the filesystem.
  :::{tip}
  To learn how to compress logs from object storage, see
  [Using object storage](../guides-using-object-storage.md).
  :::

* `<timestamp-key>` is the field path of the kv-pair that contains the timestamp in each log event.
    * E.g., if your log events look like
      `{"timestamp": {"iso8601": "2024-01-01 00:01:02.345", ...}}`, you should enter
      `timestamp.iso8601` as the timestamp key.

  :::{caution}
  Log events without the specified timestamp key will _not_ have an assigned timestamp. Currently,
  these events can only be searched from the command line (when you don't specify a timestamp
  filter).
  :::

* `<path...>` are paths to JSON log files or directories containing such files.
    * Each JSON log file should contain each log event as a [separate JSON object][json-log-format],
      i.e., _not_ as an array.

# Sample logs

For some sample logs, check out the open-source [datasets](../resources-datasets.md).

# Examining compression statistics

The compression script used above will output the compression ratio of each dataset you compress, or
you can use the UI to view overall statistics.

[json-log-format]: ../quick-start-cluster-setup/index.md#clp-json
