window.addEventListener('load', () => {

	[...document.querySelectorAll('a.delete-a')]
		.forEach(btn => {
			btn.addEventListener('click', (e) => {
				e.preventDefault();
				e.stopPropagation();

				if (confirm(`Are you sure you want to delete token ${btn.dataset.id}?`)) {
					fetch(`token?id=${btn.dataset.id}`, {
						method: "DELETE",
						credentials: 'include'
					}).then(r => {
						if (r.ok)
							window.location.reload();
						else
							return r.text().then(err => { throw err; });
					}).catch(err => {
						console.log(err);
						alert("Deleting the token failed");
					});
				}
			});
		})
});

