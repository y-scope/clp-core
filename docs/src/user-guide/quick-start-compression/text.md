# Compressing unstructured text logs

To compress unstructured text logs, from inside the package directory, run:

```bash
sbin/compress.sh fs <path1> [<path2> ...]
```

`<path...>` are paths to unstructured text log files or directories containing such files.

# Sample logs

For some sample logs, check out the open-source [datasets](../resources-datasets.md).

# Examining compression statistics

The compression script used above will output the compression ratio of each dataset you compress, or
you can use the UI to view overall statistics.
