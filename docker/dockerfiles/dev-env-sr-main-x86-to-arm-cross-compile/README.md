# Dev env for cross-compiling SR main on x86 for ARM

This image can be used so that our x86 remote compilation machines can cross-compile SR to ARM with our distributed compiler [homcc](https://github.com/celonis/homcc).

This is using the latest SR dev container from main.

## Usage
```bash
./run.sh    # This will build the image and run it so that homccd can use it