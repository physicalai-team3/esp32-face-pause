# 플레이어 페이지 정적 서빙. 실제 MQTT 접속·유튜브 재생은 브라우저(player.js)가 한다.
FROM nginx:1.27-alpine

# nginx 공식 이미지는 시작 시 /docker-entrypoint.d/*.sh 를 실행한다 → .env 값으로 config.js 생성
COPY 40-fp-config.sh /docker-entrypoint.d/40-fp-config.sh
RUN chmod +x /docker-entrypoint.d/40-fp-config.sh

COPY public/ /usr/share/nginx/html/
