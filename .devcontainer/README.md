# Utilizing the dev-container in the CLI
There are some ways to use dev-containers directly with your IDE, but here we only take a look at using it from the commandline. 
Have a look into the [developer setup guide](https://celonis.roadie.so/docs/default/system/celostar/how-to/developer-setup/) if you want to set it up for your IDE.

When using any of the following scripts you need to have [devcontainers/cli](https://github.com/devcontainers/cli) installed.
The simplest way to achieve this is with running 
```bash
npm install -g @devcontainers/cli
```
For more details you should have a look into the devcontainers/cli documentation.

## Usage
You can use a dev-container either to automatically compile, directly connect to it with shell or just start it to use it however you want.
See below how either of the options work.

### Automatic compilation
The simplest way to compile the backend is with this dev-container is running
```bash
./scripts/compile_be.sh    
```
which builds and starts the dev-container, compiles the sr-backend and stops the dev container again.
If need be, you can override the following parameters:

* `-p` the preset that is defaulted to  `Default`

Using the flags may look like this:
```bash
./compile_be.sh -p Debug
```

### Connecting to the dev-container shell
Builds and starts a dev-container, and connects a shell to it.
When the shell is `exit`ed, the docker container is stopped.

You can run
```bash
./scripts/connect_to_dev_container.sh
```
to be able to execute any commands in the shell that is created.

The path that was set as workspace is available under `/workspaces/celostar-starrocks`.

### Starting dev-container
If you want to build and start a dev container and want to have the freedom to connect to it and stop it however you like, you can do so with `build_and_start_dev_container.sh`.
As above, the script has the options:

* `-c` the place where the `devcontainer.json` is situated is defaulted to `./devcontainer.json`,
* `-w` sets the workspace, it is defaulted to `../` so you don't need to do anything if calling the script from the directory it is situated in

It returns the container_id so that you could reuse it if you need to.

A possible usage is
```bash
container_id=$(./scripts/build_and_start_dev_container.sh)
echo ${container_id}
```

You have the full flexibility to do anything with the started dev-container.

### Executing the build script
> :warning: This is not recommended, as it does not utilize the cmake presets.

It runs similar to the automatic compilation, but only runs the build script which might result in issues.
You can override the following parameters:

* `-b` the build_type, which is defaulted to `Release`
* `-o` compilation options, which is defaulted to `--be`
* `-j` number of jobs is set to 60 by default

Using the flags may look like this:
```bash
./execute_build_script.sh -b Debug -o "--be --fe --clean"
```
