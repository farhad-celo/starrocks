To deploy the udf, follow the following steps.

1. Build the fat package

$ mvn package

The artifact `duplicate-invoice-checker-udf-1.0-SNAPSHOT-jar-with-dependencies.jar` should be in the target directory.

2. Go to target directory

$ cd target


3. Serve the artifact through the http server by simply running the following command

$ python2 -m SimpleHTTPServer 7000

or

$ python3 -m http.server 7000

Note that 7000 is used as the port number in the above commands, you can choose a different port number of desired.
