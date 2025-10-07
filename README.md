# LxDrTemp
Linux Driver Temperature Demo

## Development Environment

### Prerequisites

- Docker

### Installation

1. **Build the Docker image:**

   Open a terminal in the project root directory (`LxDrTemp`) and run the following command. This will create a Docker image named `lxdrtemp-dev` with all the necessary tools and dependencies.

   ```bash
   docker build -t lxdrtemp-dev .
   ```

2. **Run the Docker container:**

   After the image is built, start an interactive container session. This command mounts the current project directory on your host machine into the `/usr/src/app` directory inside the container. This means any changes you make to the files in the project directory will be reflected inside the container.

   ```bash
   # For Windows (Command Prompt)
   docker run -it -v "%cd%:/usr/src/app" lxdrtemp-dev /bin/bash

   # For Windows (PowerShell)
   docker run -it -v "${PWD}:/usr/src/app" lxdrtemp-dev /bin/bash

   # For Linux or macOS
   docker run -it -v "$(pwd):/usr/src/app" lxdrtemp-dev /bin/bash
   ```

   You will now be inside the container's shell, ready to compile and work with the driver.