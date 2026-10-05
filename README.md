
# H87-PRO Bios Updater Linux

Updates the Intel ME firmware on an ASUS H87-PRO to 9.0.30.1482 (latest) on Linux. The official updater provided by ASUS is Windows only.

## AI Slop Warning

**USE AT YOUR OWN RISK!**

The code in this repository is 99% AI generated (using Codex with GPT 6.1 Sol by reverse engineering the original driver) and no guarantees are made whatsoever regarding its safety or functionality. However, it works on my machine (tested on Proxmox VE).

## Build
Clone this repository, and compile `bios-updater` using

```sh
./build.sh
```

### (Optional) Build firmware.bin from original firmware

`firmware.bin` contains all the partitions from the original firmware installer that are needed for the updater. It was created with `generate_firmware.py` and you can recreate it as such:

1. Download `H87-PRO BIOS 2102 and BIOS updater` from the [official ASUS support page](https://www.asus.com/supportonly/h87pro/helpdesk_bios/) and move the zip file inside the repository
2. Run `python3 generate_firmware.py H87-PRO-ASUS-2102_and_BIOS-updater.zip`

## Usage

1. If your BIOS is not on version 2104 already, update it first with the EZ Flash utility. It will not work otherwise.

2. Run the check first to confirm

   ```sh
   sudo ./bios-updater
   ```

   The output should be: `Check validated, run --flash to update`.

3. Flash the new firmware

   ```sh
   sudo ./bios-updater --flash | tee bios-updater.log
   ```

   Do NOT turn off your computer or stop the application while it's updating.  

4. Reboot and run the check as in step 2 again. Now, it should output `ME is already 9.0.30.1482` if the update succeeded.

