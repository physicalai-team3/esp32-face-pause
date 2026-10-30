# 가짜 ESP32: 진짜 보드와 같은 형식으로 presence/status/telemetry 를 발행한다
FROM python:3.12-slim

WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY fake_esp32.py .

CMD ["python", "fake_esp32.py"]
