setws [lindex $argv 0]

projects -clean -type bsp -name cosmos_bsp
projects -build -type bsp -name cosmos_bsp

projects -clean -type app -name cosmos_app
projects -build -type app -name cosmos_app
