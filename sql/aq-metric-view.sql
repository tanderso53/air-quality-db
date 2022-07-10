-- View to query individual metrics
create or replace view aqfeather.metrics as
  select q1.devname, q1.updatetime, q1.sensorname,
	 (jsonb_path_query ->> 'name')::text as metric,
	 (jsonb_path_query ->> 'timemillis')::int as timemillis,
	 (jsonb_path_query ->> 'value')::numeric as value,
	 (jsonb_path_query ->> 'unit')::text as unit
    from (
      select updatetime, jsonb_path_query ->> 'sensor' as sensorname,
	     jsonb_path_query -> 'data' as dataarray, devname
	from aqfeather.rawjson,
	     lateral jsonb_path_query(datastring, '$.output[*]')
       order by updatetime, sensorname, dataarray) q1,
	 lateral jsonb_path_query(q1.dataarray, '$[*]')
   order by devname, updatetime, sensorname, metric;
