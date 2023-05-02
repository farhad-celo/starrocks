cd /celostar-starrocks;

echo "./build.sh --fe --clean";
./build.sh --fe --clean;

echo "./build.sh --be --clean -j `nproc`";
./build.sh --be --clean -j `nproc`;

echo "mvn clean package -f celonis/udf/duplicate-invoice-checker/pom.xml";
mvn clean package -f celonis/udf/duplicate-invoice-checker/pom.xml;