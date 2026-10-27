FROM python:3.12-slim
WORKDIR /app
RUN pip install --no-cache-dir pillow==11.0.0
COPY server.py .
ENV CLOCK_DB=/data/clock.db PORT=8078
VOLUME /data
EXPOSE 8078
CMD ["python", "server.py", "serve", "--host", "0.0.0.0"]
