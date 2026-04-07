# Cosmos+ OpenSSD Project Guide  
*Sogang University – Embedded Systems Software (CSE4116, Prof. Youngjae Kim)*

---

## Development Workflow

0. **Initial Project Creation**
   Create the initial OpenSSD firmware project for development environment setup:
   ```
   ./util/make_bsp_app_2019.sh
   ```
   > **Important:** This script must be executed from the workspace directory (i.e., run the command while your current working directory is `workspace/`).
   > Before running this script, the workspace directory contains only:
   > - `README.md  setup.sh  util/`
   > After successful execution, a new project directory workspace/GreedyFTL is generated.

1. **Firmware Development**  
   Write and modify firmware source code in:  
   ```
   ./GreedyFTL/cosmos_app/src/
   ```

2. **Build the Firmware**  
   Compile the firmware program:  
   ```bash
   ./setup.sh GreedyFTL build
   ```

3. **Track Debug Messages**  
   Monitor UART messages printed from the OpenSSD board:  
   ```bash
   sudo ./setup.sh GreedyFTL show
   ```
   > **Note:** The load/show step requires USB JTAG/UART bridges to be detected.  
   > You should see the following devices in `lsusb`:  
   > - `10c4:ea60 Silicon Labs CP210x UART Bridge`  
   > - `0403:6014 FT232H Single HS USB-UART/FIFO IC`  
   > If either device is **not detected**, the load/show process may fail. Try reconnecting the USB cables or using a different USB port, and confirm with:  
   > ```bash
   > lsusb
   > ``

4. **Load the Bitstream**  
   Load the compiled bitstream (firmware binary) onto the OpenSSD board:  
   ```bash
   ./setup.sh GreedyFTL load
   ```

5. **Trigger PCIe Re-enumeration**  
   Re-scan the PCIe bus to detect the OpenSSD board:
   ```bash
   ./setup.sh GreedyFTL scan
   ```

   > **Usage Note:**  
   > - When you **load the firmware for the very first time after connecting the OpenSSD board to the host machine**, you must run:  
   >   ```bash
   >   ./setup.sh GreedyFTL load
   >   sudo poweroff
   >   ```  
   >   Then, press the machine’s physical **power button** to fully reboot the system.  
   > - For all **subsequent firmware loads**, a full reboot is no longer required. Instead, you can simply trigger PCIe re-enumeration with:  
   >   ```bash
   >   ./setup.sh GreedyFTL scan
   >   ```  
   >   This allows firmware replacement **without rebooting** the machine.


6. **Enjoy Testing & Debugging**
   Have fun experimenting with your firmware! Tweak, test, and debug freely --- and see what your Cosmos+ OpenSSD can do!

---

## Important Notes

- **Do not modify** `setup.sh` or any files under `util/` unless you are studying them purely out of curiosity.  
- **Use `sudo` sparingly.** Only use administrative privileges when absolutely necessary.  
  - When prompted, enter your current account password.  
- Be extremely careful when using destructive commands such as:  
  ```bash
  sudo rm -rf /
  ```  
  This will completely destroy your operating system and require a full reinstall. Any damage caused is solely your responsibility.

---

