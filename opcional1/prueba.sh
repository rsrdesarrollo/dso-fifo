#! /bin/bash

while [ True ]; do
	echo "remove 1" > /proc/modlist
done & 
bucle1=$!	


while [ True ]; do
	echo "add 1" > /proc/modlist
done &
bucle2=$!	


while [ True ]; do
	echo "cleanup" > /proc/modlist
done &
bucle3=$!


while [ True ]; do 
	cat /proc/modlist > /dev/null
done &
bucle4=$!


read -p "Presione enter para salir..." var

kill $bucle1 $bucle2 $bucle3 $bucle4
