# Using object storage

CLP can:

* [compress logs from object storage](#compressing-logs-from-object-storage) (e.g., S3);
* [store archives on object storage](#storing-archives-on-object-storage); and
* [view the compressed logs from object storage](#viewing-compressed-logs-from-object-storage).

This guide explains how to configure CLP for all three use cases.

:::{note}
Currently, only the [clp-json][release-choices] release supports object storage. Support for
`clp-text` will be added in a future release.
:::

:::{note}
Currently, CLP only supports using S3 as object storage. Support for other object storage services
will be added in a future release.
:::

## Compressing logs from object storage

To compress logs from S3, you'll need to:

1. Set up an AWS IAM user that CLP can use to access the bucket containing your logs.
2. Use the `s3` subcommand of `sbin/compress.sh` to compress your logs.

### Setting up an AWS IAM user

To set up a user:

1. Create a user by following [this guide][aws-create-iam-user].
    * If you already have a user to use for ingesting logs, you can skip this step.
2. Attach the following policy to the user by following [this guide][add-iam-policy].

    ```json
    {
       "Version": "2012-10-17",
       "Statement": [
           {
               "Effect": "Allow",
               "Action": "s3:GetObject",
               "Resource": [
                   "arn:aws:s3:::<bucket-name>/<key-prefix>/*"
               ]
           },
           {
               "Effect": "Allow",
               "Action": [
                   "s3:ListBucket"
               ],
               "Resource": [
                   "arn:aws:s3:::<bucket-name>"
               ],
               "Condition": {
                   "StringLike": {
                       "s3:prefix": "<key-prefix>/*"
                   }
               }
           }
       ]
    }
    ```
   
    Replace the fields in angle brackets (`<>`) with the appropriate values:
    * `<bucket-name>` should be the name of the S3 bucket containing your logs.
    * `<key-prefix>` should be the prefix of all logs you wish to compress.

    :::{warning}
    To follow the [principle of least privilege][least-privilege-principle], ensure the user doesn't
    have other unnecessary permission policies attached. If the user does have other policies,
    consider creating a new user with only the permission policy above.
    :::

### Using `sbin/compress.sh s3`

You can use the `s3` subcommand as follows:

```bash
sbin/compress.sh s3 --aws-credentials-file <credentials-file> s3://<bucket-name>/<key-prefix>
```

* `<credentials-file>` is the path to an AWS credentials file like the following:

    ```ini
    [default]
    aws_access_key_id = <aws-access-key-id>
    aws_secret_access_key = <aws-secret-access-key>
    ```

    * CLP expects the credentials to be in the `default` section.
    * `<aws-access-key-id>` and `<aws-secret-access-key>` are the access key ID and secret access
      key of the IAM user you set up in the previous section.

* `<bucket-name>` is the name of the S3 bucket containing your logs.
* `<key-prefix>` is the path prefix of all logs you wish to compress.

:::{note}
The `s3` subcommand only supports a single URL but will compress any logs that have the given key
prefix.

If you wish to compress a single log file, specify the entire path to the log file. However, if that
log file's path is a prefix of another log file's path, then both log files will be compressed. This
limitation will be addressed in a future release.
:::

## Storing archives on object storage

To configure CLP to store archives on S3, update the `archive_output.storage` key in
`<package>/etc/clp-config.yml`:

```yaml
archive_output:
  storage:
    type: "s3"
    staging_directory: "var/data/staged-archives"  # Or a path of your choosing
    s3_config:
      region_code: "<region-code>"
      bucket: "<bucket-name>"
      key_prefix: "<key-prefix>"
      credentials:
        access_key_id: "<aws-access-key-id>"
        secret_access_key: "<aws-secret-access-key>"

  # archive_output's other config keys
```

* `s3_config` configures both the S3 bucket where archives should be stored and the credentials
  for accessing it.
  * `<region-code>` is the AWS region [code][aws-region-codes] for the bucket.
  * `<bucket-name>` is the bucket's name.
  * `<key-prefix>` is the "directory" where all archives will be stored within the bucket and
    must end with `/`.
  * `credentials` contains the S3 credentials necessary for accessing the bucket.

    :::{note}
    These credentials can be for a different IAM user than the one set up in the previous section,
    as long as they can access the bucket.
    :::

## Viewing compressed logs from object storage

To configure CLP to be able to view compressed log files from S3, you'll need to configure a bucket
where CLP can store intermediate files that the log viewer can open. To do so, update the
`stream_output.storage` key in `<package>/etc/clp-config.yml`:

```yaml
stream_output:
  storage:
    type: "s3"
    staging_directory: "var/data/staged-streams"  # Or a path of your choosing
    s3_config:
      region_code: "<region-code>"
      bucket: "<bucket-name>"
      key_prefix: "<key-prefix>"
      credentials:
        access_key_id: "<aws-access-key-id>"
        secret_access_key: "<aws-secret-access-key>"

  # stream_output's other config keys
```

The configuration keys above function identically to those in `archive_output.storage`, except they
should be configured to use a different S3 path (i.e., a different key-prefix in the same bucket or
a different bucket entirely).

:::{note}
To view compressed log files, clp-text currently converts them into IR streams that the log viewer
can open, while clp-json converts them into JSONL streams. These streams only need to be stored for
as long as the streams are being viewed, but CLP currently doesn't explicitly delete the streams.
This limitation will be addressed in a future release.
:::

[add-iam-policy]: https://docs.aws.amazon.com/IAM/latest/UserGuide/access_policies_manage-attach-detach.html#embed-inline-policy-console
[aws-create-iam-user]: https://docs.aws.amazon.com/IAM/latest/UserGuide/id_users_create.html
[aws-region-codes]: https://docs.aws.amazon.com/AmazonRDS/latest/UserGuide/Concepts.RegionsAndAvailabilityZones.html#Concepts.RegionsAndAvailabilityZones.Availability
[least-privilege-principle]: https://en.wikipedia.org/wiki/Principle_of_least_privilege
[release-choices]: quick-start-cluster-setup/index.md#choosing-a-release
