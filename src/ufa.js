addToLibrary({
	$ufa_init__postset: 'ufa_init();',
	$ufa_init: () => {
		const handles = new Map();
		let nextHandle = 0;

		function allocHandle(obj) {
			const handle = nextHandle++;
			handles.set(handle, obj);
			return handle;
		}

		function closeHandle(handle) {
			handles.delete(handle);
		}

		function resolveHandle(handle) {
			return handles.get(handle);
		}

		const UFA_OK = 0;
		const UFA_PENDING = 1;
		const UFA_CANCELLED = 2;
		const UFA_ERROR = 3;

		function allocString(alloc_ctx, str) {
			const len = lengthBytesUTF8(str) + 1;
			const buf = _ufa_web_malloc(alloc_ctx, len);
			stringToUTF8(str, buf, len);
			return buf;
		}

		function reportError(obj, e) {
			console.error(e);
			setValue(obj.error_ptr, allocString(obj.alloc_ctx, String(e)), '*');
		}

		_ufa_web_begin_open_file = (filter, alloc_ctx, filename_ptr, status_ptr, error_ptr) => {
			const input = document.createElement('input');
			input.type = 'file';

			const obj = {
				input,
				alloc_ctx,
				error_ptr,
				content: null,
				pos: 0,
			};

			function openSuccess(file) {
				obj.file = file;

				setValue(filename_ptr, allocString(alloc_ctx, file.name), '*');

				file.arrayBuffer().then((buf) => {
					obj.content = new Uint8Array(buf);
					setValue(status_ptr, UFA_OK, 'i8');
				}).catch((e) => {
					reportError(obj, e);
					setValue(status_ptr, UFA_ERROR, 'i8');
				})
			}

			function openFailure() {
				setValue(status_ptr, UFA_CANCELLED);
			}

			input.addEventListener("change", (event) => {
				if (input.files.length === 1) {
					openSuccess(input.files[0]);
				} else {
					openFailure();
				}
			}, {once: true});

			input.addEventListener("cancel", (event) => {
				openFailure();
			}, {once: true});

			input.accept = UTF8ToString(filter);
			console.log(input.accept);
			input.click();

			return allocHandle(obj);
		};

		_ufa_web_end_open_file = (handle) => {
			closeHandle(handle);
		};

		_ufa_web_read_open_file = (handle, buf, size_ptr) => {
			const file = resolveHandle(handle);
			try {
				if (!file || !file.content) {
					if (file) { reportError(file, new Error('The file is not ready for reading')); }
					HEAPU32[size_ptr >> 2] = 0;
					return UFA_ERROR;
				}

				const bytesRequested = HEAPU32[size_ptr >> 2];

				if (file.pos >= file.content.byteLength) {
					HEAPU32[size_ptr >> 2] = 0;
					return UFA_OK;
				}

				const bytesAvailable = file.content.byteLength - file.pos;
				const bytesToRead = Math.min(bytesRequested, bytesAvailable);

				HEAPU8.set(file.content.subarray(file.pos, file.pos + bytesToRead), buf);
				file.pos += bytesToRead;

				HEAPU32[size_ptr >> 2] = bytesToRead;
				return UFA_OK;
			} catch (e) {
				if (file) { reportError(file, e); } else { console.error(e); }
				return UFA_ERROR;
			}
		};

		_ufa_web_get_default_name = (alloc_ctx) => {
			let name = (document.title || '')
				.replace(/[\\/:*?"<>|\x00-\x1f]/g, '_')
				.trim();
			if (name === '') { name = 'ufa'; }

			return allocString(alloc_ctx, name);
		};

		_ufa_web_begin_save_file = (filename, accept, show_dialog, alloc_ctx, filename_ptr, status_ptr, error_ptr) => {
			const obj = {
				filename: UTF8ToString(filename),
				alloc_ctx,
				error_ptr,
				mode: null,
				chunks: [],
				writable: null,
				pending: null,
				error: null,
			};

			function startDownload() {
				obj.mode = 'download';
				setValue(status_ptr, UFA_OK, 'i8');
			}

			if (show_dialog && window.showSaveFilePicker) {
				const options = { suggestedName: obj.filename };
				const extensions = UTF8ToString(accept);
				if (extensions !== '') {
					options.types = [{
						accept: { 'application/octet-stream': extensions.split(',') },
					}];
				}

				window.showSaveFilePicker(options).then(
					(handle) => handle.createWritable().then((writable) => {
						obj.mode = 'picker';
						obj.writable = writable;
						obj.pending = Promise.resolve();

						// Report the name the user actually chose
						obj.filename = handle.name;
						setValue(filename_ptr, allocString(alloc_ctx, handle.name), '*');

						setValue(status_ptr, UFA_OK, 'i8');
					}).catch((e) => {
						reportError(obj, e);
						setValue(status_ptr, UFA_ERROR, 'i8');
					}),
					(e) => {
						if (e.name === 'AbortError') {
							setValue(status_ptr, UFA_CANCELLED, 'i8');
						} else {
							// e.g: SecurityError when called outside of a user
							// gesture or in a cross-origin iframe
							console.error(e);
							startDownload();
						}
					}
				);
			} else {
				startDownload();
			}

			return allocHandle(obj);
		};

		_ufa_web_end_save_file = (handle) => {
			const file = resolveHandle(handle);
			if (!file) { return; }

			if (file.writable) {
				file.pending
					.then(() => file.writable.close())
					.catch((e) => { console.error(e); });
			} else if (file.mode === 'download') {
				const blob = new Blob(file.chunks, { type: 'application/octet-stream' });
				const url = URL.createObjectURL(blob);

				const a = document.createElement('a');
				a.href = url;
				a.download = file.filename;
				a.click();

				URL.revokeObjectURL(url);
			}

			closeHandle(handle);
		};

		_ufa_web_write_save_file = function(handle, buf, size_ptr) {
			const file = resolveHandle(handle);
			try {
				if (!file || file.error) { return UFA_ERROR; }

				const bytesToWrite = HEAPU32[size_ptr >> 2];
				const chunk = HEAPU8.slice(buf, buf + bytesToWrite);
				if (file.writable) {
					// The stream serializes writes; a failure surfaces on the
					// next call
					file.pending = file.pending
						.then(() => file.writable.write(chunk))
						.catch((e) => {
							reportError(file, e);
							file.error = e;
						});
				} else {
					file.chunks.push(chunk);
				}
				return UFA_OK;
			} catch (e) {
				if (file) { reportError(file, e); } else { console.error(e); }
				return UFA_ERROR;
			}
		};
	},

	ufa_web_begin_open_file: () => {},
	ufa_web_begin_open_file__deps: ['$ufa_init'],

	ufa_web_end_open_file: () => {},
	ufa_web_end_open_file__deps: ['$ufa_init'],

	ufa_web_read_open_file: () => {},
	ufa_web_read_open_file__deps: ['$ufa_init'],

	ufa_web_get_default_name: () => {},
	ufa_web_get_default_name__deps: ['$ufa_init'],

	ufa_web_begin_save_file: () => {},
	ufa_web_begin_save_file__deps: ['$ufa_init'],

	ufa_web_end_save_file: () => {},
	ufa_web_end_save_file__deps: ['$ufa_init'],

	ufa_web_write_save_file: () => {},
	ufa_web_write_save_file__deps: ['$ufa_init'],
});
