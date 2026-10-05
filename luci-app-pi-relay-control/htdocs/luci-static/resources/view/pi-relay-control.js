'use strict';
'require form';
'require rpc';
'require ui';
'require view';

const callStatus = rpc.declare({
	object: 'luci.pi-relay-control',
	method: 'status',
	expect: { relays: [] }
});

const callSet = rpc.declare({
	object: 'luci.pi-relay-control',
	method: 'set',
	params: ['section', 'state']
});

return view.extend({
	load: function() {
		return callStatus();
	},

	render: function(relays) {
		let m, s, o;

		m = new form.Map('pi-relay-control', _('Relay Control'),
			_('Configure GPIO relays controlled by the pi-relay-control daemon. ' +
			  'Changes below require Save & Apply (which restarts the daemon); ' +
			  'use the On/Off buttons in the status table for immediate control ' +
			  'without a restart.'));

		s = m.section(form.TypedSection, 'relay', _('Relays'));
		s.anonymous = true;
		s.addremove = true;

		o = s.option(form.Value, 'gpio_pin', _('GPIO pin (BCM)'));
		o.datatype = 'uinteger';
		o.rmempty = false;

		o = s.option(form.Value, 'port', _('TCP port'));
		o.datatype = 'port';
		o.rmempty = false;

		o = s.option(form.Flag, 'always_on', _('Always on at boot'),
			_('Force this relay ON every time the daemon starts, ignoring whatever state was last persisted.'));

		return m.render().then(L.bind(function(mapNode) {
			return E('div', {}, [
				this.renderStatusTable(relays),
				mapNode
			]);
		}, this));
	},

	renderStatusTable: function(relays) {
		const rows = (relays || []).map(L.bind(function(r) {
			const isOn = r.state === 'on';
			const isUnknown = r.state === 'unknown';

			return E('tr', { 'class': 'tr' }, [
				E('td', { 'class': 'td' }, r.section),
				E('td', { 'class': 'td' }, r.gpio_pin != null ? 'GPIO ' + r.gpio_pin : '-'),
				E('td', { 'class': 'td' }, r.port != null ? String(r.port) : '-'),
				E('td', { 'class': 'td' }, isUnknown ? _('unreachable') : (isOn ? _('ON') : _('OFF'))),
				E('td', { 'class': 'td cbi-section-actions' }, [
					E('button', {
						'class': 'cbi-button cbi-button-positive',
						// null, not false: dom.js's attr() only skips an
						// attribute for null/undefined, so a bare `false`
						// here would render as disabled="false" -- which
						// HTML treats as disabled regardless of the
						// string value, permanently disabling this
						// button even when it should be clickable.
						'disabled': (isUnknown || isOn) || null,
						'click': ui.createHandlerFn(this, 'handleToggle', r.section, 'on')
					}, _('On')),
					' ',
					E('button', {
						'class': 'cbi-button cbi-button-negative',
						'disabled': (isUnknown || !isOn) || null,
						'click': ui.createHandlerFn(this, 'handleToggle', r.section, 'off')
					}, _('Off'))
				])
			]);
		}, this));

		return E('div', { 'class': 'cbi-section' }, [
			E('h3', {}, _('Live status')),
			E('table', { 'class': 'table cbi-section-table' }, [
				E('tr', { 'class': 'tr table-titles' }, [
					E('th', { 'class': 'th' }, _('Section')),
					E('th', { 'class': 'th' }, _('GPIO')),
					E('th', { 'class': 'th' }, _('Port')),
					E('th', { 'class': 'th' }, _('State')),
					E('th', { 'class': 'th' }, _('Control'))
				])
			].concat(rows))
		]);
	},

	handleToggle: function(section, state) {
		return callSet(section, state).then(function() {
			location.reload();
		}).catch(function(err) {
			ui.addNotification(null, E('p', _('Failed to set relay state: %s').format(err.message || err)));
		});
	}
});
