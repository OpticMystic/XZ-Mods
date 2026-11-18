//! Safely remove a USB drive the app just wrote: flush and dismount the volume, then ask the
//! system to eject the device, the same request the tray's "Safely remove" sends.

/// Accepts only a bare drive root ("E:\" or "E:") on Windows and a mount point under /Volumes on macOS.
fn parse(volume: &str) -> Result<String, String> {
    #[cfg(windows)]
    {
        let bytes = volume.as_bytes();
        let ok = (bytes.len() == 2 || (bytes.len() == 3 && (bytes[2] == b'\\' || bytes[2] == b'/')))
            && bytes[0].is_ascii_alphabetic() && bytes[1] == b':';
        if !ok || matches!(bytes[0].to_ascii_uppercase(), b'A' | b'B' | b'C') {
            return Err("Only a USB drive can be ejected.".into());
        }
        Ok(format!("{}:", bytes[0].to_ascii_uppercase() as char))
    }
    #[cfg(not(windows))]
    {
        let path = std::path::Path::new(volume);
        if !path.starts_with("/Volumes/") || path.components().count() != 3 {
            return Err("Only a USB drive can be ejected.".into());
        }
        Ok(volume.trim_end_matches('/').to_string())
    }
}

#[tauri::command]
pub async fn eject_volume(volume: String) -> Result<(), String> {
    let target = parse(&volume)?;
    tauri::async_runtime::spawn_blocking(move || platform::eject(&target))
        .await
        .map_err(|error| error.to_string())?
}

#[cfg(windows)]
mod platform {
    use std::{ffi::c_void, ptr::null_mut};

    type Handle = *mut c_void;
    const INVALID: Handle = -1isize as Handle;
    const GENERIC_READ: u32 = 0x8000_0000;
    const GENERIC_WRITE: u32 = 0x4000_0000;
    const SHARE_RW: u32 = 3;
    const OPEN_EXISTING: u32 = 3;
    const FSCTL_LOCK_VOLUME: u32 = 0x0009_0018;
    const FSCTL_DISMOUNT_VOLUME: u32 = 0x0009_0020;
    const IOCTL_STORAGE_GET_DEVICE_NUMBER: u32 = 0x002D_1080;
    const IOCTL_STORAGE_QUERY_PROPERTY: u32 = 0x002D_1400;
    // STORAGE_BUS_TYPE values for USB, SD and MMC readers.
    const EJECTABLE_BUSES: [u32; 3] = [7, 0x0C, 0x0D];
    const DIGCF_PRESENT: u32 = 2;
    const DIGCF_DEVICEINTERFACE: u32 = 0x10;
    const CR_SUCCESS: u32 = 0;
    const DRIVE_REMOVABLE: u32 = 2;
    const DRIVE_FIXED: u32 = 3;

    #[repr(C)]
    struct Guid(u32, u16, u16, [u8; 8]);
    const GUID_DEVINTERFACE_DISK: Guid = Guid(0x53f5_6307, 0xb6bf, 0x11d0, [0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b]);
    #[repr(C)]
    #[derive(Default, Clone, Copy, PartialEq)]
    struct DeviceNumber { device_type: u32, number: u32, partition: u32 }
    #[repr(C)]
    struct InterfaceData { size: u32, class: [u8; 16], flags: u32, reserved: usize }
    #[repr(C)]
    struct DevinfoData { size: u32, class: [u8; 16], devinst: u32, reserved: usize }

    #[link(name = "kernel32")]
    extern "system" {
        fn CreateFileW(name: *const u16, access: u32, share: u32, security: *mut c_void, disposition: u32, flags: u32, template: Handle) -> Handle;
        fn DeviceIoControl(device: Handle, code: u32, input: *mut c_void, input_size: u32, output: *mut c_void, output_size: u32, returned: *mut u32, overlapped: *mut c_void) -> i32;
        fn CloseHandle(handle: Handle) -> i32;
        fn GetDriveTypeW(root: *const u16) -> u32;
        fn GetLastError() -> u32;
        fn GetLogicalDrives() -> u32;
    }
    #[link(name = "setupapi")]
    extern "system" {
        fn SetupDiGetClassDevsW(class: *const Guid, enumerator: *const u16, parent: Handle, flags: u32) -> Handle;
        fn SetupDiEnumDeviceInterfaces(set: Handle, info: *mut c_void, class: *const Guid, index: u32, data: *mut InterfaceData) -> i32;
        fn SetupDiGetDeviceInterfaceDetailW(set: Handle, data: *mut InterfaceData, detail: *mut c_void, size: u32, required: *mut u32, info: *mut DevinfoData) -> i32;
        fn SetupDiDestroyDeviceInfoList(set: Handle) -> i32;
    }
    #[link(name = "cfgmgr32")]
    extern "system" {
        fn CM_Get_Parent(parent: *mut u32, devinst: u32, flags: u32) -> u32;
        fn CM_Request_Device_EjectW(devinst: u32, veto: *mut u32, veto_name: *mut u16, veto_len: u32, flags: u32) -> u32;
    }

    fn wide(text: &str) -> Vec<u16> { text.encode_utf16().chain(Some(0)).collect() }

    struct Owned(Handle);
    impl Drop for Owned { fn drop(&mut self) { unsafe { CloseHandle(self.0); } } }

    fn open(path: &[u16], access: u32) -> Option<Owned> {
        let handle = unsafe { CreateFileW(path.as_ptr(), access, SHARE_RW, null_mut(), OPEN_EXISTING, 0, null_mut()) };
        (handle != INVALID).then_some(Owned(handle))
    }
    fn ioctl(handle: &Owned, code: u32, output: *mut c_void, size: u32) -> bool {
        let mut returned = 0;
        unsafe { DeviceIoControl(handle.0, code, null_mut(), 0, output, size, &mut returned, null_mut()) != 0 }
    }
    /// STORAGE_DEVICE_DESCRIPTOR.BusType (offset 28) from a StorageDeviceProperty standard query.
    fn bus_type(handle: &Owned) -> Option<u32> {
        let mut query = [0u32; 3];
        let mut descriptor = [0u8; 1024];
        let mut returned = 0;
        let ok = unsafe { DeviceIoControl(handle.0, IOCTL_STORAGE_QUERY_PROPERTY, query.as_mut_ptr() as *mut c_void, 12,
            descriptor.as_mut_ptr() as *mut c_void, descriptor.len() as u32, &mut returned, null_mut()) != 0 };
        (ok && returned >= 32).then(|| u32::from_le_bytes([descriptor[28], descriptor[29], descriptor[30], descriptor[31]]))
    }
    fn device_number(handle: &Owned) -> Option<DeviceNumber> {
        let mut number = DeviceNumber::default();
        ioctl(handle, IOCTL_STORAGE_GET_DEVICE_NUMBER, &mut number as *mut _ as *mut c_void, std::mem::size_of::<DeviceNumber>() as u32)
            .then_some(number)
    }

    /// The device instance of the disk whose storage device number matches the volume's.
    fn disk_devinst(wanted: DeviceNumber) -> Option<u32> {
        let set = unsafe { SetupDiGetClassDevsW(&GUID_DEVINTERFACE_DISK, std::ptr::null(), null_mut(), DIGCF_PRESENT | DIGCF_DEVICEINTERFACE) };
        if set == INVALID { return None; }
        let mut found = None;
        for index in 0.. {
            let mut data = InterfaceData { size: std::mem::size_of::<InterfaceData>() as u32, class: [0; 16], flags: 0, reserved: 0 };
            if unsafe { SetupDiEnumDeviceInterfaces(set, null_mut(), &GUID_DEVINTERFACE_DISK, index, &mut data) } == 0 { break; }
            let mut required = 0;
            unsafe { SetupDiGetDeviceInterfaceDetailW(set, &mut data, null_mut(), 0, &mut required, null_mut()); }
            if required < 8 { continue; }
            // SP_DEVICE_INTERFACE_DETAIL_DATA_W: a u32 cbSize (8 on 64-bit, 6 on 32-bit) then the UTF-16 path.
            let mut buffer = vec![0u64; (required as usize).div_ceil(8)];
            unsafe { *(buffer.as_mut_ptr() as *mut u32) = if cfg!(target_pointer_width = "64") { 8 } else { 6 }; }
            let mut info = DevinfoData { size: std::mem::size_of::<DevinfoData>() as u32, class: [0; 16], devinst: 0, reserved: 0 };
            if unsafe { SetupDiGetDeviceInterfaceDetailW(set, &mut data, buffer.as_mut_ptr() as *mut c_void, required, null_mut(), &mut info) } == 0 { continue; }
            let path = unsafe { (buffer.as_ptr() as *const u8).add(4) as *const u16 };
            let length = (0..).take_while(|&i| unsafe { *path.add(i) } != 0).count();
            let path: Vec<u16> = unsafe { std::slice::from_raw_parts(path, length) }.iter().copied().chain(Some(0)).collect();
            if let Some(disk) = open(&path, 0) {
                if device_number(&disk).is_some_and(|number| number.device_type == wanted.device_type && number.number == wanted.number) {
                    found = Some(info.devinst);
                    break;
                }
            }
        }
        unsafe { SetupDiDestroyDeviceInfoList(set); }
        found
    }

    fn mounted(letter: u8) -> bool { let mask = unsafe { GetLogicalDrives() }; mask & (1u32 << (letter - b'A')) != 0 }

    pub fn eject(drive: &str) -> Result<(), String> {
        let letter = drive.as_bytes()[0];
        let kind = unsafe { GetDriveTypeW(wide(&format!("{drive}\\")).as_ptr()) };
        if kind != DRIVE_REMOVABLE && kind != DRIVE_FIXED { return Err(format!("{drive} is not a USB drive this app can eject.")); }
        let number = {
            let volume = open(&wide(&format!("\\\\.\\{drive}")), GENERIC_READ | GENERIC_WRITE)
                .ok_or_else(|| format!("{drive} could not be opened to eject it. Use Safely Remove Hardware in the taskbar instead."))?;
            // Internal disks report as fixed drives too; only a drive on a USB or card-reader bus is ever dismounted here.
            if !bus_type(&volume).is_some_and(|bus| EJECTABLE_BUSES.contains(&bus)) {
                return Err(format!("{drive} is not a USB drive, so XZ Mods will not eject it."));
            }
            let number = device_number(&volume).ok_or("Windows did not say which device this USB is. Use Safely Remove Hardware in the taskbar instead.")?;
            // The lock fails while any program has a file open on the USB; that is the case to report plainly.
            if !ioctl(&volume, FSCTL_LOCK_VOLUME, null_mut(), 0) {
                let code = unsafe { GetLastError() };
                return Err(format!("Another program is still using {drive} (Windows error {code}). Close any window showing the USB, then eject again."));
            }
            ioctl(&volume, FSCTL_DISMOUNT_VOLUME, null_mut(), 0);
            number
        };
        let disk = disk_devinst(number).ok_or("The USB device could not be found to eject it. Use Safely Remove Hardware in the taskbar instead.")?;
        let mut parent = 0;
        if unsafe { CM_Get_Parent(&mut parent, disk, 0) } != CR_SUCCESS { parent = disk; }
        let mut last = String::new();
        for attempt in 0..3 {
            let mut veto = 0u32;
            let mut name = [0u16; 260];
            let result = unsafe { CM_Request_Device_EjectW(parent, &mut veto, name.as_mut_ptr(), name.len() as u32, 0) };
            if result == CR_SUCCESS && veto == 0 {
                for _ in 0..40 {
                    if !mounted(letter) { return Ok(()); }
                    std::thread::sleep(std::time::Duration::from_millis(100));
                }
                return Ok(());
            }
            let who = String::from_utf16_lossy(&name[..name.iter().position(|&c| c == 0).unwrap_or(0)]);
            last = if who.is_empty() { format!("Windows refused to eject {drive} (code {result}, veto {veto}).") }
                   else { format!("Windows refused to eject {drive} because {who} is using it.") };
            if attempt < 2 { std::thread::sleep(std::time::Duration::from_millis(500)); }
        }
        Err(last + " Close anything using the USB, then eject again.")
    }
}

#[cfg(target_os = "macos")]
mod platform {
    pub fn eject(volume: &str) -> Result<(), String> {
        let output = std::process::Command::new("/usr/sbin/diskutil").args(["eject", volume]).output().map_err(|error| error.to_string())?;
        if output.status.success() { Ok(()) } else {
            Err(format!("macOS did not eject the USB: {}", String::from_utf8_lossy(&output.stderr).trim()))
        }
    }
}

#[cfg(not(any(windows, target_os = "macos")))]
mod platform {
    pub fn eject(_volume: &str) -> Result<(), String> { Err("Ejecting is not supported on this system. Unmount the USB yourself.".into()) }
}

#[cfg(test)]
mod tests {
    use super::parse;
    #[test]
    #[cfg(windows)]
    fn only_bare_removable_drive_roots_are_accepted() {
        assert_eq!(parse("e:\\").unwrap(), "E:");
        assert_eq!(parse("F:").unwrap(), "F:");
        for bad in ["C:\\", "E:\\folder", "\\\\.\\E:", "", "EE:", "../"] { assert!(parse(bad).is_err(), "{bad}"); }
    }
    #[test]
    #[cfg(not(windows))]
    fn only_volume_mount_points_are_accepted() {
        assert!(parse("/Volumes/XZ").is_ok());
        for bad in ["/", "/Volumes", "/Users/x", "/Volumes/XZ/sub"] { assert!(parse(bad).is_err(), "{bad}"); }
    }
}
