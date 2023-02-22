// script for admin area

class SortableTable {
	constructor(table) {
		this.table = table;

		[...table.querySelectorAll('th')].forEach(th =>
			th.addEventListener('click', (e) => this.sortOnColumn(e)));
	}

	sortOnColumn(e) {
		e.preventDefault();

		const th = e.target;
		this.sortColumn(th);
	}

	sortColumn(th) {
		const tr = th.parentNode;
		const ths = Array.from(tr.getElementsByTagName("th"));
		const ix = ths.indexOf(th);

		const kt = tr.dataset.keytype.split('|');

		ths.forEach(th => th.classList.remove("sorted-asc", "sorted-desc"));

		const rowArray = [...this.table.querySelectorAll('tr.sorted')];

		let desc = !this.sortDescending;
		if (this.sortedOnColumn !== ix) {
			desc = false;
			this.sortedOnColumn = ix;
		}
		this.sortDescending = desc;

		rowArray.sort((a, b) => {
			let ka = Array.from(a.children)[ix].innerText;
			let kb = Array.from(b.children)[ix].innerText;

			let d = 0;

			if (ka != kb) {
				switch (kt[ix]) {
					case 'b':
						break;

					case 's':
						ka = ka.toLowerCase();
						kb = kb.toLowerCase();
						d = ka.localeCompare(kb);
						break;

					case 'i':
					case 'f':
						ka = parseFloat(ka);
						kb = parseFloat(kb);

						if (Number.isNaN(ka) || Number.isNaN(kb))
							d = Number.isNaN(ka) ? -1 : 1;
						else
							d = ka - kb;
						break;
				}
			}

			return desc ? -d : d;
		});

		const tbody = this.table.querySelector('tbody');
		rowArray.forEach(row => tbody.appendChild(row));

		th.classList.add(desc ? "sorted-desc" : "sorted-asc");
	}
}

window.addEventListener('load', () => {

	const tabel = document.querySelector('table');
	new SortableTable(tabel);

	[...document.querySelectorAll('tr.job-row')]
		.forEach(tr => {
			tr.addEventListener('click', (e) => {
				e.preventDefault();

				const [_, user, id] = tr.dataset.runId.match(/^(.+?)-(\d+)$/);
				window.location = `admin/job/${user}/${id}`;
			});
		});

	[...document.querySelectorAll('a.delete-a')]
		.forEach(btn => {
			btn.addEventListener('click', (e) => {
				e.preventDefault();
				e.stopPropagation();

				if (confirm(`Are you sure you want to delete row ${btn.dataset.nr}?`))
					window.location = `admin/delete/${tab}/${btn.dataset.id}`;
			});
		});
});

