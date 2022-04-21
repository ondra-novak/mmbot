"use strict";
var avail_langs = [
	["en","data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAEgAAAAwBAMAAABAsiHYAAAAMFBMVEUAG2oAIWvKACrIEzBPWYHOT1bRYGnXfICdnq7bm6Djqava19vz1dTy3uH5+Pj9//yRXFQ4AAABZ0lEQVQ4y+2VIU9DMRDH/1+BZAJdEgSSy0jATU/OokCj0XyBZ/AI8FN4FBqHnR0T4wPAOPra3r3rrSQTBMUlL3vt+9/11+7uijtmftxT4weiMx7G080lnWP8wvx185Nof31Pp++ID/PHpC0aPb1SjIIYLS64bIu61QFFHuQfxapECSh+RA6oWFaUgSIM9G3iRSP1h8ZcelGnJLCDSmScYcNakcWAG4uo8oSLLKKKAY6xiOrdwO02i9y5wJ1bFrkTRrHp5iL08bNoFUJaXb6ysyRy9ouia2czorGfA+1g/yLa7TD/+A+WpIvvbyHMVfQcDheadCV9Y+p+zuhK0jem7i0drSV9SyHIbCkE8bGF0PXx6WQxlFS/Oh3PTUmZKS1OdSsiG3woc4MFNx4ahvGEi2xaz8AAx2ibmDrD7bZqh4IBd25VYxV/WKCtFl1IYIG2m30OAQvUuDYSDCxQ4wJKWLBArausx/oG0moFWcbnpY4AAAAASUVORK5CYII="],
	["cs","data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAEgAAAAwBAMAAABAsiHYAAAAMFBMVEXXAAAAQn8CRXwYRX3NGSfXFxo5QnqtKkp5OGg6XYxBX4qLmrOOnLXK0drv9Pb9//ye4wQMAAAAzElEQVQ4y43SzQ3CMAyG4ewBA3AtlhA/547gGTJBN8gajMAKGYEVOkJGAJVWIXUSf/7Oz+GVZRc/eO4+GxAdkwHRy4KGtwHRbTYgmPVD9LQgkLUi0q+1ITokA1KzMhqiAWlZf6RkFaifVSKKFtTL2iF6JAPq/JZA7SyJmlkSNbMq1MqqkXf1pLmOXE2iy8QYBcbIM0atIImaQRIFxsgzRp2gHeoFlegcGCPPGJ1GjJSgjLSgjDxjpAZtSA9aEQhakWeMUNCCYNCCAjb8BbPXr46aiiSEAAAAAElFTkSuQmCC"]
];

async function formTest() {
	var def = await (await fetch("/api/admin/forms")).json();
	var form = create_form(def.trader, "trader", "init");
	form.open();
}

class App {
	
    lang = new Lang();
	page_root = null;	
	
	constructor() {
		this.langobj = new Lang();
		
		
	}
	
	async start() {
		await this.chooselang();
		on_login();
		
		this.page_root = load_template("root");
		document.body.appendChild(this.page_root.root);
		
	}


	async chooselang() {
		const lang = localStorage["lang"];
		if (!lang) {
			var l = await chooselang_dlg(avail_langs);
			localStorage["lang"] = l;
			this.langobj.load(l);
		} else {
			return this.langobj.load(lang);
		}
	}

	async chooselang_dlg() {
		var l = await chooselang_dlg(avail_langs);
		localStorage["lang"] = l;
		this.chooselang();
	}
	
	async on_login(usr) {
		try {
			if (!usr) usr = await ui_fetch("/user");
			var data = {
				uname: usr.user,
				lang:{"!click":this.chooselang_dlg.bind(this)},
				pwd:{".hidden":!usr.exists,
					 "!click":this.change_pwd.bind(this)},
				logout:{".hidden":!usr.exists && !usr.jwt,
					   "!click":this.logout.bind(this)},
				login:{".hidden":usr.exists,
					   "!click":ui_login
					  },
				usermenu:{classList:{
					exists:usr.exists,
					jwt:usr.jwt
				}}
			}
			
			this.page_root.setData(data);
		} catch (e) {
			errorDialog(e);		
		}
	}

	async change_pwd() {
		try {
			await dialog_change_pwd(async (data,dlg)=>{
				try {
					await post_json("/user/set_password", {
						old:data.old_password,
						new:data.new_password,
					})
					return true;
				} catch (e) {
					if (e.io_error && e.req.status == 409) {
						dlg.mark("not_match");						
					} else {
						errorDialog(e);
					}
					return false;
				}				
			});
			await dialogBox([{type:"label",name:"pwd_changed"},
							{type:"button",bottom:true,name:"ok"}],
							"pwd_changed","pwd_changed");

		} catch(e) {}
	}

	async logout() {
		await dialogBox([
			{name:"ask_logout",type:"label"},
			{name:"ok",type:"button",bottom:true},
			{name:"cancel",type:"button",bottom:true}
		],"logout_dlg","Confirm logout");
		try {
			await ui_fetch("/user",{method:"DELETE"});
			this.on_login({"user":"","exists":false});
		}catch (e) {
			errorDialog(e);
		}
	}
}

var theApp = new App();

function start() {
	theApp.start();
}

async function on_login(usr) {
	await theApp.on_login(usr);
}

	
	
	
	
	
	
	
