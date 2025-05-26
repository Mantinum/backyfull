# BackyFull

BackyFull is a multi-platform backup software (macOS, Windows, Linux) designed for scheduling and performing backups to local storage.

## Features (Milestone 1)

*   **Scheduling:** Configure daily automated backups at a specific time.
*   **Graphical User Interface (GUI):**
    *   Select source and local destination directories.
    *   Set the desired time for daily backups.
    *   Manually trigger an immediate backup.
    *   View basic logs of backup operations and application events.
*   **Command Line Interface (CLI):**
    *   Perform one-shot backups of a single file or files in the root of a directory.
*   **Persistence:** Backup schedules (source, destination, time) configured via the GUI are saved and reloaded when the application starts.
*   **Local Storage Target:** Initial support for backing up to a local directory.

## Build Instructions

CMake is required to build BackyFull.

1.  **Configure the project:**
    Create a build directory and run CMake from there:
    ```bash
    mkdir build
    cd build
    cmake .. 
    ```
    Or, for out-of-source builds from the project root:
    ```bash
    cmake -S . -B build
    ```

2.  **Build the project:**
    After configuration, build the executables:
    ```bash
    cmake --build build
    ```
    This will compile the CLI, GUI, and test executables.

## How to Run

### GUI Application
After building, you can run the GUI application from the build directory:
```bash
./build/backy_full_gui
```
Or, on Windows, you might run `.\build\Release\backy_full_gui.exe` or `.\build\Debug\backy_full_gui.exe` depending on your build type.

### Command Line Interface (CLI)
The CLI for one-shot backups is still available. Run it from the build directory:
```bash
./build/backy_x_cli --src <source_path> --dst <destination_path>
```
Example:
```bash
./build/backy_x_cli --src /path/to/myImportantFile.txt --dst /mnt/myBackupDrive/backups
```

## Running Tests
Unit tests are available for different components. After building the project, navigate to the build directory and run CTest:
```bash
cd build  # If not already in the build directory
ctest --output-on-failure
```
This will run all registered tests, including:
*   `LocalTargetTests`: Tests for the local storage target functionality.
*   `SchedulerTests`: Tests for the backup scheduling logic and persistence.

## Contributing
(Placeholder for future contribution guidelines)

## License
(Placeholder for license information - e.g., MIT, GPL)
