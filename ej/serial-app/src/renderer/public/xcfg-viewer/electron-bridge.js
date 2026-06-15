(function () {
  if (!window.xcfgViewerAPI) return;

  function toPath(url) {
    if (typeof url === 'string') return url;
    if (url && typeof url.url === 'string') return url.url;
    return String(url || '');
  }

  async function serializeBody(body) {
    if (!body) return undefined;
    if (body instanceof FormData) {
      const files = [];
      const fields = {};
      for (const [key, value] of body.entries()) {
        if (value instanceof File || (value && typeof value.arrayBuffer === 'function')) {
          const buf = await value.arrayBuffer();
          files.push({
            field: key,
            name: value.name || 'file',
            mime: value.type || '',
            data: Array.from(new Uint8Array(buf))
          });
        } else {
          fields[key] = String(value);
        }
      }
      return { __formData: true, fields, files };
    }
    if (typeof body === 'string') {
      try {
        return JSON.parse(body);
      } catch (_) {
        return { content: body };
      }
    }
    return body;
  }

  const nativeFetch = window.fetch.bind(window);

  window.fetch = async function (input, init) {
    const rawPath = toPath(input);
    const pathOnly = rawPath.split('?')[0];
    const isApi = pathOnly.startsWith('/api/') || pathOnly.startsWith('/metadata_images/');
    if (!isApi) return nativeFetch(input, init);

    const method = String((init && init.method) || 'GET').toUpperCase();
    const body = await serializeBody(init && init.body);
    const result = await window.xcfgViewerAPI.request({
      path: rawPath,
      method,
      body
    });

    if (result && result.binary && result.contentType) {
      const bytes = new Uint8Array(result.binary);
      return new Response(bytes, {
        status: result.status || 200,
        headers: { 'Content-Type': result.contentType }
      });
    }

    const payload = result && Object.prototype.hasOwnProperty.call(result, 'data') ? result.data : result;
    const status = (result && result.status) || 200;
    return new Response(JSON.stringify(payload ?? {}), {
      status,
      headers: { 'Content-Type': 'application/json' }
    });
  };
})();
