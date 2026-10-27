# SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>
#
# SPDX-License-Identifier: GPL-3.0-or-later

FROM python:3.12-slim
WORKDIR /app
COPY pyproject.toml ./
COPY kareer_server ./kareer_server
RUN pip install --no-cache-dir . && useradd --uid 1000 --no-create-home kareer \
    && mkdir /data && chown kareer /data
USER kareer
EXPOSE 8000
HEALTHCHECK --interval=60s --timeout=5s CMD python -c "import urllib.request; urllib.request.urlopen('http://127.0.0.1:8000/health')"
CMD ["kareer-server"]
