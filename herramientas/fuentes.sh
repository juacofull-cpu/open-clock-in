#!/bin/sh
# Letras de la pantalla: Poppins , 4 bpp, Latin-1.
# Los TTF de Poppins (Google Fonts, licencia OFL) se bajan de https://fonts.google.com/specimen/Poppins.
# Los símbolos (LV_SYMBOL_*) salen de las Montserrat de respaldo.
set -e
TTF=${POPPINS_DIR:-fuentes/poppins}
OUT="$(cd "$(dirname "$0")/../src/fuentes" && pwd)"
LATIN="-r 0x20-0x7E,0xA0-0xFF"
conv() { # nombre ttf tamaño rango respaldo
  npx -y lv_font_conv@1.5.3 --no-compress --format lvgl --bpp 4 --lv-include lvgl.h \
    --font "$TTF/$2" --size "$3" $4 --lv-font-name "$1" ${5:+--lv-fallback $5} -o "$OUT/$1.c"
}
conv f_reloj  Poppins-Bold.ttf     128 "-r 0x2D,0x30-0x3A"
conv f_titulo Poppins-Bold.ttf     44  "$LATIN" lv_font_montserrat_28
conv f_grande Poppins-Medium.ttf   24  "$LATIN" lv_font_montserrat_20
conv f_texto  Poppins-Regular.ttf  18  "$LATIN" lv_font_montserrat_16
conv f_fino   Poppins-Light.ttf    22  "$LATIN" lv_font_montserrat_20
conv f_chico  Poppins-Medium.ttf   14  "$LATIN" lv_font_montserrat_14
