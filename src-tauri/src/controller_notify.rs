// A Windows notification when the XZ sends a report while the app is open. Clicking it brings the window forward
// and opens that report in My reports. The renderer decides when to ask (once per report ID, never after the DJ
// stops reminders or after 7 days); this side refuses anything that is not a pending report that still reminds.
// When Windows will not show it (notifications off for the app, a portable copy with no Start menu entry, or
// another platform) the reply says so and the window keeps to its badge and callout.
use crate::{controller_queue, support};
use serde_json::{json, Value};

pub(crate) const OPEN_EVENT:&str="xz-controller-report-open";

fn escape(text:&str)->String {text.replace('&',"&amp;").replace('<',"&lt;").replace('>',"&gt;").replace('"',"&quot;").replace('\'',"&apos;")}
pub(crate) fn toast_xml(reference:&str)->String {
    format!("<toast launch=\"{}\"><visual><binding template=\"ToastGeneric\"><text>Your XZ sent a report, {}</text><text>Add what happened?</text></binding></visual></toast>",
        escape(&format!("xz-report:{reference}")),escape(reference))
}
// Only a report that is pending, still reminding and has this exact reference may raise a notification.
pub(crate) fn may_notify(queue:&Value,id:&str,reference:&str)->bool {
    support::valid_id(id) && crate::support_contract::parse_report_reference(reference).and_then(crate::support_contract::format_report_reference).as_deref()==Some(reference)
        && queue["pending"].as_array().is_some_and(|rows|rows.iter().any(|row|row["report_id"]==id&&row["report_reference"]==reference&&row["remind"]==true&&row["completion_pending"]!=true))
}

#[cfg(windows)] mod native {
    use std::sync::Mutex;
    use tauri::{Emitter,Manager};
    use windows::{core::{HSTRING,IInspectable},Data::Xml::Dom::XmlDocument,Foundation::TypedEventHandler,UI::Notifications::{NotificationSetting,ToastNotification,ToastNotificationManager}};
    // Windows raises the click on a notification object this process still holds, so the last few stay alive.
    static SHOWN:Mutex<Vec<ToastNotification>>=Mutex::new(Vec::new());
    pub(super) fn installed()->bool {std::env::current_exe().ok().and_then(|p|p.parent().map(|d|d.join("uninstall.exe").is_file())).unwrap_or(false)}
    pub(super) fn show(app:&tauri::AppHandle,id:&str,xml:&str)->Result<Option<&'static str>,String> {
        let notifier=ToastNotificationManager::CreateToastNotifierWithId(&HSTRING::from(app.config().identifier.as_str())).map_err(|_|"Notifications unavailable")?;
        if notifier.Setting().map_err(|_|"Notifications unavailable")?!=NotificationSetting::Enabled {return Ok(Some("off"));}
        let document=XmlDocument::new().map_err(|_|"Notifications unavailable")?;
        document.LoadXml(&HSTRING::from(xml)).map_err(|_|"Invalid notification")?;
        let toast=ToastNotification::CreateToastNotification(&document).map_err(|_|"Notifications unavailable")?;
        toast.SetTag(&HSTRING::from(&id[..16])).map_err(|_|"Notifications unavailable")?;
        let handle=app.clone();let report=id.to_owned();
        toast.Activated(&TypedEventHandler::<ToastNotification,IInspectable>::new(move|_,_|{
            if let Some(window)=handle.get_webview_window("main") {let _=window.unminimize();let _=window.show();let _=window.set_focus();}
            let _=handle.emit(super::OPEN_EVENT,serde_json::json!({"id":report}));
            Ok(())
        })).map_err(|_|"Notifications unavailable")?;
        notifier.Show(&toast).map_err(|_|"Notifications unavailable")?;
        if let Ok(mut shown)=SHOWN.lock() {shown.push(toast);let extra=shown.len().saturating_sub(8);shown.drain(..extra);}
        Ok(None)
    }
}

#[tauri::command] pub fn support_notify_pending(app:tauri::AppHandle,id:String,reference:String)->Result<Value,String> {
    if !may_notify(&controller_queue::list(&support::root(&app)?),&id,&reference) {return Err("This report does not need a reminder".into());}
    #[cfg(windows)] {
        if !native::installed() {return Ok(json!({"shown":false,"reason":"not_installed"}));}
        return Ok(match native::show(&app,&id,&toast_xml(&reference)) {
            Ok(None)=>json!({"shown":true}),Ok(Some(reason))=>json!({"shown":false,"reason":reason}),Err(_)=>json!({"shown":false,"reason":"unavailable"})
        });
    }
    #[cfg(not(windows))] Ok(json!({"shown":false,"reason":"unsupported"}))
}

#[cfg(test)] mod tests {
    use super::*;
    #[test] fn notification_names_the_reference_and_escapes_markup() {
        let xml=toast_xml("R-4F7K-2M9Q");
        assert!(xml.contains("<text>Your XZ sent a report, R-4F7K-2M9Q</text><text>Add what happened?</text>"));
        assert!(toast_xml("<a&\"b>").contains("&lt;a&amp;&quot;b&gt;"));
        assert!(!xml.contains('\u{2014}'));
    }
    #[test] fn only_a_reminding_pending_report_can_notify() {
        let id="a".repeat(32);
        let queue=|remind:bool,completion:bool|json!({"pending":[{"report_id":id,"report_reference":"R-4F7K-2M9Q","remind":remind,"completion_pending":completion}]});
        assert!(may_notify(&queue(true,false),&id,"R-4F7K-2M9Q"));
        assert!(!may_notify(&queue(false,false),&id,"R-4F7K-2M9Q"),"dismissed or expired reminders never notify");
        assert!(!may_notify(&queue(true,true),&id,"R-4F7K-2M9Q"),"a saved completion needs no reminder");
        assert!(!may_notify(&queue(true,false),&id,"R-4F7K-2M9R"),"the reference must match");
        assert!(!may_notify(&queue(true,false),&"b".repeat(32),"R-4F7K-2M9Q"));
        assert!(!may_notify(&queue(true,false),&id,"r-4f7k-2m9q"),"only the canonical reference is shown");
        assert!(!may_notify(&json!({"pending":[]}),"not-an-id","R-4F7K-2M9Q"));
    }
}
