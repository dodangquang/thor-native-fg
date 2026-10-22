#!/system/bin/sh
magisk --sqlite "SELECT key,value FROM settings WHERE key='zygisk';"
magisk --sqlite "SELECT key,value FROM settings WHERE key='denylist';"
