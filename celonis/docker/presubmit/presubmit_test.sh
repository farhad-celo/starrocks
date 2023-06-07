cd /celostar-starrocks;

echo "./build.sh --fe --clean";
./build.sh --fe --clean;

echo "./build.sh --be --clean -j `nproc`";
./build.sh --be --use-staros --clean -j `nproc`;

echo "./run-be-ut.sh";
./run-be-ut.sh --use-staros --clean -j `nproc`;

echo "./run-fe-ut.sh";
export FE_UT_PARALLEL=32;
./run-fe-ut.sh;

echo "mvn clean package -f celonis/udf/duplicate-invoice-checker/pom.xml";
mvn clean package -f celonis/udf/duplicate-invoice-checker/pom.xml;