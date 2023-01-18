To deploy the udf, follow the following steps.
Build the fat package:
$ mvn package
The artifact `duplicate-invoice-checker-udf-1.0-SNAPSHOT-jar-with-dependencies.jar` should be in the target directory.
$ cd target
Serve the artifact through the http server by simply running the following command:
$ python2 -m SimpleHTTPServer 7000
