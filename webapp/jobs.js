function submitJob(form, e) {
	e.preventDefault();

	const data = new FormData(form);

	fetch(form.action, {
		method: "POST",
		body: data
	}).then(response => {
		if (response.ok)
			window.location = "job";
		else
			throw "Server replied with an error";
	}).catch(err => {
		console.log(err);
		alert("Failed to submit job: " + err);
	});
}

window.addEventListener('load', () => {

	const form = document.forms['job-form'];

	const submit = form.querySelector('button[type="submit"]');
	submit.addEventListener('click', (e) => submitJob(form, e));
	form.addEventListener('submit', (e) => submitJob(form, e));

	const table = document.querySelector('#jobs-table');
	if (table) {

		const rows = table.querySelectorAll("tr.done");
		Array.from(rows).forEach(tr => {
			tr.addEventListener('click', (e) => {
				const jobID = tr.dataset.job;
				window.location = `job/result/${jobID}`;
			})
		});

		[...document.querySelectorAll('a.delete-a')]
			.forEach(btn => {
				btn.addEventListener('click', (e) => {
					e.preventDefault();
					e.stopPropagation();

					let failed = false;

					if (confirm(`Are you sure you want to delete job ${btn.dataset.id}?`)) {
						fetch(`job/${btn.dataset.id}`, {
							method: "DELETE",
							credentials: 'include'
						}).then(r => {
							if (r.ok)
								window.location.replace("job");
							else
								failed = true;
							return r.text();
						}).then(r => {
							if (failed)
								throw r;
						}).catch(err => {
							console.log(err);
						});
					}
				});
			});
	}
});
