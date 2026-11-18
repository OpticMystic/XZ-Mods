// The main window opens at the size the pages are laid out for, and never larger than the screen it opens on.
// tauri.conf.json holds the wanted size and the smallest size; this clamps both to the work area of the monitor
// under the mouse pointer (the taskbar or dock stays clear) and centres the window there. A 1366 x 768 screen at
// 125 % gives about 1092 x 582; the pages reflow down to 760 x 520 (ui/style.css, the narrow rail).
use tauri::{PhysicalPosition, PhysicalSize, Size};

/// Wanted and smallest inner size, in logical pixels, fitted to the room a screen leaves for the inside of the
/// window (also logical pixels). Returns (size, minimum).
pub fn fit(wanted:(f64,f64),smallest:(f64,f64),room:(f64,f64))->((f64,f64),(f64,f64)){
    let room=(room.0.max(1.0).floor(),room.1.max(1.0).floor());
    let size=(wanted.0.min(room.0),wanted.1.min(room.1));
    (size,(smallest.0.min(size.0),smallest.1.min(size.1)))
}

/// Sizes and centres a window built hidden. Any failure leaves the window as tauri.conf.json made it.
pub fn place(app:&tauri::App,window:&tauri::WebviewWindow,wanted:(f64,f64),smallest:(f64,f64))->tauri::Result<()>{
    let pointer=app.cursor_position().ok().and_then(|at|app.monitor_from_point(at.x,at.y).ok().flatten());
    let Some(monitor)=pointer.or(app.primary_monitor()?) else {return Ok(())};
    let scale=monitor.scale_factor();
    let area=*monitor.work_area();
    let (outer,inner)=(window.outer_size()?,window.inner_size()?);
    // The frame and title bar, so the whole window fits, not only its inside.
    let frame=(outer.width.saturating_sub(inner.width) as f64,outer.height.saturating_sub(inner.height) as f64);
    let room=((area.size.width as f64-frame.0)/scale,(area.size.height as f64-frame.1)/scale);
    let ((width,height),(min_width,min_height))=fit(wanted,smallest,room);
    let physical=|w:f64,h:f64|PhysicalSize::new((w*scale).round() as u32,(h*scale).round() as u32);
    let centre=|size:PhysicalSize<u32>|{
        let (w,h)=(size.width as f64+frame.0,size.height as f64+frame.1);
        PhysicalPosition::new(area.position.x+((area.size.width as f64-w)/2.0).max(0.0) as i32,area.position.y+((area.size.height as f64-h)/2.0).max(0.0) as i32)
    };
    // Move onto the monitor first: a move across monitors with different scaling can resize the window.
    window.set_position(centre(physical(width,height)))?;
    window.set_min_size(Some(Size::Physical(physical(min_width,min_height))))?;
    window.set_size(physical(width,height))?;
    window.set_position(centre(physical(width,height)))?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::fit;
    const WANTED:(f64,f64)=(1240.0,900.0);
    const SMALLEST:(f64,f64)=(760.0,520.0);
    #[test]
    fn a_large_screen_gets_the_wanted_size(){
        assert_eq!(fit(WANTED,SMALLEST,(2560.0,1400.0)),((1240.0,900.0),(760.0,520.0)));
    }
    #[test]
    fn a_small_screen_gets_a_window_that_fits(){
        // 1366 x 768 at 125 %, a 48 px taskbar, a 31 px title bar: about 1092 x 545 inside.
        assert_eq!(fit(WANTED,SMALLEST,(1092.8,545.6)),((1092.0,545.0),(760.0,520.0)));
        // 1280 x 720 at 100 %, a 40 px taskbar.
        assert_eq!(fit(WANTED,SMALLEST,(1280.0,649.0)),((1240.0,649.0),(760.0,520.0)));
    }
    #[test]
    fn a_screen_smaller_than_the_minimum_lowers_the_minimum(){
        assert_eq!(fit(WANTED,SMALLEST,(700.0,480.0)),((700.0,480.0),(700.0,480.0)));
    }
}
