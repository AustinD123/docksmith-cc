# docksmith-cc

Docksmith is a simplified Docker-like build and runtime system built from scratch in C++.

## Start Fresh After Cloning

This repository does not track generated build output. After cloning, build everything locally:

1. Enter the project:

```powershell
cd docksmith
```

2. Configure CMake into a local build directory:

```powershell
cmake -S . -B build
```

3. Build the binaries:

```powershell
cmake --build build
```

4. Run integration tests:

```powershell
./build/docksmith_integration_phases.exe
```

5. Run the CLI:

```powershell
./build/docksmith.exe help
```

## Sample App Flow

From the repository root:

```powershell
cd sample-app
../docksmith/build/docksmith.exe build -t myapp:latest .
../docksmith/build/docksmith.exe run myapp:latest
../docksmith/build/docksmith.exe images
../docksmith/build/docksmith.exe rmi myapp:latest
```

Note: runtime isolation requires Linux for container execution.
