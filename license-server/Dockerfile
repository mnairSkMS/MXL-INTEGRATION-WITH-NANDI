# Use the official Python image as a base
FROM python:3.11-slim

# Create a non-root user
RUN useradd -m -s /bin/bash appuser

# Set the working directory in the container
WORKDIR /license_server

# Create necessary directories
RUN mkdir -p /license_server/certificates

# Copy requirements and install dependencies
COPY requirements.txt /license_server/
RUN pip install --no-cache-dir -r /license_server/requirements.txt

# Copy application files
COPY app /license_server/app
COPY version.txt /license_server/
COPY start.sh /license_server/start.sh

RUN chmod +x /license_server/start.sh

# Set proper ownership
RUN chown -R appuser:appuser /license_server

# Switch to non-root user
USER appuser

# Expose the port for the session app
EXPOSE 8000

# Use the script as the entry point
CMD ["/license_server/start.sh"]