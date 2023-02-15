window.addEventListener('load', () => {

	[...document.querySelectorAll('a.delete-a')]
		.forEach(btn => {
			btn.addEventListener('click', (e) => {
				e.preventDefault();
				e.stopPropagation();

				if (confirm(`Are you sure you want to delete session ${btn.dataset.id}?`))
				{
					fetch(`sessions?id=${btn.dataset.id}`, {
						method: "DELETE",
						credentials: 'include'
					}).then(r => {
						if (r.ok)
							window.location.reload();
						else
							return r.text();
					}).then(r => {
						console.log(r);
						alert("Deleting the session failed");
					});
				}
			});
		})
});

