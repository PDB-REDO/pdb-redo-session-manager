// script for admin area

function deleteSession(sessionID) {

	const fd = new FormData();
	fd.append("sessionid", sessionID);

	fetch('/admin/deleteSession', {
		method: "DELETE",
		body: fd
	}).then(r => {
		if (r.ok)
			window.location.reload();
		else
			alert("delete failed");
	});
}


window.addEventListener('load', () => {

	[...document.querySelectorAll('td > a.action')]
		.forEach(a => {
			a.addEventListener('click', (e) => {
				e.preventDefault();

				const sessionID = a.dataset["sessionid"];
				deleteSession(sessionID);
			});
		})
});

