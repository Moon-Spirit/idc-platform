# ═══════════════════════════════════════════════════════════════════════════════
# IDC Platform — Nginx
# ═══════════════════════════════════════════════════════════════════════════════
# Serves the pre-built frontend dist and reverse-proxies API/WS to backend.

FROM nginx:latest

# Remove default site config
RUN rm /etc/nginx/conf.d/default.conf

# Copy custom site config
COPY docker/nginx/default.conf /etc/nginx/conf.d/

# Copy pre-built frontend distribution
COPY frontend/dist /var/www/html

EXPOSE 80

CMD ["nginx", "-g", "daemon off;"]
