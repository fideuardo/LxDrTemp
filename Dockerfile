# 1. Usar una imagen base de Ubuntu (se recomienda una versión específica)
FROM ubuntu:22.04

# 2. Evitar que apt-get pida interacción
ENV DEBIAN_FRONTEND=noninteractive

# 3. Instalar todas las dependencias para la compilación del kernel
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    make \
    gcc \
    git \
    flex \
    bison \
    libssl-dev \
    libelf-dev \
    bc

# 4. Clonar la versión específica del kernel de WSL2
# La etiqueta git corresponde a la versión del kernel 6.6.87.2-microsoft-standard-WSL2
ENV KERNEL_TAG=linux-msft-wsl-6.6.87.2
RUN git clone --depth 1 --branch ${KERNEL_TAG} https://github.com/microsoft/WSL2-Linux-Kernel.git /usr/src/WSL2-Linux-Kernel

# 5. Preparar el código fuente del kernel para compilar módulos externos
WORKDIR /usr/src/WSL2-Linux-Kernel
RUN make prepare && make scripts

# 6. Copiar el código del driver y compilarlo
WORKDIR /usr/src/driver
COPY . .
# Actualiza el Makefile para que apunte al directorio del kernel dentro del contenedor
RUN sed -i \'s|KDIR ?= .*|KDIR ?= /usr/src/WSL2-Linux-Kernel|\' Makefile
RUN make

# 7. Comando para mantener el contenedor en ejecución y poder inspeccionarlo (opcional)
CMD ["bash"]
