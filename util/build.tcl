setws [lindex $argv 0]

projects -clean -type app -name cosmos_app
projects -build -type app -name cosmos_app
