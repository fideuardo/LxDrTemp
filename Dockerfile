# Usa una imagen base de Ubuntu
FROM ubuntu:latest

# Evita que apt-get pida interacción durante la instalación
ENV DEBIAN_FRONTEND=noninteractive

# Actualiza los paquetes e instala las herramientas de compilación y cabeceras de Linux
RUN apt-get update && apt-get install -y build-essential make gcc linux-headers-generic

# Establece el directorio de trabajo dentro del contenedor
WORKDIR /usr/src/app

# Copia los archivos del proyecto al directorio de trabajo
COPY . .

# Comando por defecto para mantener el contenedor en ejecución (opcional)
CMD ["tail", "-f", "/dev/null"]
